// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"
#include "garnet/bin/gzos/ree_agent/tipc_agent.h"

namespace ree_agent {

void ReeMessageImpl::AddMessageChannel(
    fidl::VectorPtr<MessageChannelInfo> msg_chan_infos,
    AddMessageChannelCallback cb) {
  fbl::AutoLock lock(&lock_);

  for (auto& info : *msg_chan_infos) {
    if (info.id >= kMaxMsgChannels) {
      cb(ZX_ERR_INVALID_ARGS);
      return;
    }

    if (agents_[info.id] != nullptr) {
      cb(ZX_ERR_ALREADY_EXISTS);
      return;
    }

    ReeAgent* agent = nullptr;
    switch (info.type) {
      case MessageType::Tipc:
        agent = new TipcAgent(info.id, std::move(info.channel),
                              info.max_message_size, ta_service_provider_);
        break;
      default:
        cb(ZX_ERR_NOT_SUPPORTED);
        return;
    }

    if (agent == nullptr) {
      cb(ZX_ERR_NO_MEMORY);
      return;
    }

    agents_[info.id].reset(agent);
  }
  cb(ZX_OK);
}

zx_status_t ReeMessageImpl::NotifyAgentLocked(Action act, uint32_t id) {
  zx_status_t status;

  switch (act) {
    case Action::Start:
      status = agents_[id]->Start();
      break;
    case Action::Stop:
      status = agents_[id]->Stop();
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
  }
  return status;
}

zx_status_t ReeMessageImpl::NotifyAgentsLocked(Action act,
                                               fidl::VectorPtr<uint32_t> ids) {
  zx_status_t status = ZX_OK;

  if (ids.is_null()) {
    // If not specify any ids, notify all agents
    for (uint32_t id = 0; id < kMaxMsgChannels; id++) {
      if (agents_[id] == nullptr)
        continue;

      status = NotifyAgentLocked(act, id);
      if (status != ZX_OK)
        break;
    }
  } else {
    for (auto& id : *ids) {
      if (agents_[id] == nullptr) {
        status = ZX_ERR_INVALID_ARGS;
        break;
      }

      status = NotifyAgentLocked(act, id);
      if (status != ZX_OK)
        break;
    }
  }

  return status;
}

void ReeMessageImpl::Start(fidl::VectorPtr<uint32_t> ids, StartCallback cb) {
  fbl::AutoLock lock(&lock_);
  zx_status_t status = NotifyAgentsLocked(Action::Start, std::move(ids));
  cb(status);
}

void ReeMessageImpl::Stop(fidl::VectorPtr<uint32_t> ids, StopCallback cb) {
  fbl::AutoLock lock(&lock_);
  zx_status_t status = NotifyAgentsLocked(Action::Stop, std::move(ids));
  cb(status);
}

}  // namespace ree_message
