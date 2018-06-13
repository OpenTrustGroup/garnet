// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/tipc_agent.h"
#include "garnet/lib/trusty/tipc_msg.h"

namespace ree_agent {

using trusty::tipc_hdr;
using trusty::tipc_ctrl_msg_hdr;
using trusty::CtrlMessage;
using trusty::kTipcCtrlAddress;

TipcAgent::TipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size)
    : ReeAgent(id, std::move(ch), max_msg_size) {}

TipcAgent::~TipcAgent() {}

zx_status_t TipcAgent::SendMessage(uint32_t local, uint32_t remote, void* data,
                                   size_t data_len) {
  size_t msg_size = sizeof(tipc_hdr) + data_len;
  fbl::unique_ptr<char> buf(new char[msg_size]);

  if (buf == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto hdr = reinterpret_cast<tipc_hdr*>(buf.get());
  hdr->src = local;
  hdr->dst = remote;
  hdr->reserved = 0;
  hdr->len = data_len;
  hdr->flags = 0;
  memcpy(hdr->data, data, data_len);

  return WriteMessage(buf.get(), msg_size);
}

zx_status_t TipcAgent::Start() {
  tipc_ctrl_msg_hdr ctrl_msg{CtrlMessage::GO_ONLINE, 0};

  zx_status_t status = SendMessage(kTipcCtrlAddress, kTipcCtrlAddress,
                                   &ctrl_msg, sizeof(ctrl_msg));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to send tipc go online message, status="
                   << status;
    return status;
  }

  return ReeAgent::Start();
}

zx_status_t TipcAgent::Stop() {
  zx_status_t status = ReeAgent::Stop();
  if (status == ZX_ERR_BAD_STATE) {
    // tipc agent ignore error if agent is already stopped.
    return ZX_OK;
  }

  // TODO(james): disconnect all Tipc channels

  return status;
}

zx_status_t TipcAgent::HandleMessage(void* buf, size_t size) {
  // TODO(james): implementation
  return ZX_OK;
}

}  // namespace ree_agent
