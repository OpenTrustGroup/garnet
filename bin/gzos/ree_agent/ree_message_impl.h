// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <gzos/reeagent/cpp/fidl.h>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <zx/resource.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace ree_agent {

constexpr uint32_t kMaxMsgChannels = 10;

enum Action { Start, Stop };

class ReeMessageImpl : public gzos::reeagent::ReeMessage {
 public:
  ReeMessageImpl(TaServices& service_provider, zx_handle_t shm)
      : binding_(this), ta_service_provider_(service_provider), shm_handle_(shm) {}

  ReeMessageImpl() = delete;

  void Bind(zx::channel from_trusty_virtio) {
    binding_.Bind(std::move(from_trusty_virtio));
  }

  void AddMessageChannel(
      fidl::VectorPtr<gzos::reeagent::MessageChannelInfo> msg_chan_infos,
      AddMessageChannelCallback cb) override;

  void Start(fidl::VectorPtr<uint32_t> ids, StartCallback cb) override;
  void Stop(fidl::VectorPtr<uint32_t> ids, StopCallback cb) override;

 private:
  zx_status_t NotifyAgentLocked(Action act, uint32_t id)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  zx_status_t NotifyAgentsLocked(Action act, fidl::VectorPtr<uint32_t> ids)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  fbl::Mutex lock_;
  fidl::Binding<gzos::reeagent::ReeMessage> binding_;
  fbl::unique_ptr<Agent> agents_[kMaxMsgChannels] FXL_GUARDED_BY(lock_);
  TaServices& ta_service_provider_;

  zx_handle_t shm_handle_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReeMessageImpl);
};

}  // namespace ree_agent
