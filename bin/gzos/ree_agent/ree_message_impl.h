// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ree_agent/cpp/fidl.h>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace ree_agent {

constexpr uint32_t kMaxMsgChannels = 10;

enum Action { Start, Stop };

class ReeMessageImpl : public ReeMessage {
 public:
  ReeMessageImpl(TaServices& service_provider)
      : binding_(this), ta_service_provider_(service_provider) {}

  ReeMessageImpl() = delete;

  void Bind(zx::channel from_trusty_virtio) {
    binding_.Bind(std::move(from_trusty_virtio));
  }

  void AddMessageChannel(fidl::VectorPtr<MessageChannelInfo> msg_chan_infos,
                         AddMessageChannelCallback cb) override;

  void Start(fidl::VectorPtr<uint32_t> ids, StartCallback cb) override;
  void Stop(fidl::VectorPtr<uint32_t> ids, StopCallback cb) override;

 private:
  zx_status_t NotifyAgentLocked(Action act, uint32_t id)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  zx_status_t NotifyAgentsLocked(Action act, fidl::VectorPtr<uint32_t> ids)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  fbl::Mutex lock_;
  fidl::Binding<ReeMessage> binding_;
  fbl::unique_ptr<ReeAgent> agents_[kMaxMsgChannels] FXL_GUARDED_BY(lock_);
  TaServices& ta_service_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReeMessageImpl);
};

}  // namespace ree_message
