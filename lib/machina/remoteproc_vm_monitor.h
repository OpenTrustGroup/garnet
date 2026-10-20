// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "garnet/bin/guest/proto/rproc.pb.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/remoteproc.h"
#include "lib/app/cpp/application_context.h"
#include "lib/async/cpp/task.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class PeerStateNotifier {
 public:
  virtual ~PeerStateNotifier() {}
  virtual int16_t peer_vmid() = 0;
  virtual bool is_connected() = 0;
  virtual void peer_online() = 0;
  virtual void peer_offline() = 0;
};

class RprocVmStateMonitor : public VmStateListener {
 public:
  RprocVmStateMonitor(int16_t vmid, async_t* async)
      : binding_(this), async_(async), vmid_(vmid) {}

  virtual void VmOnline(int16_t vmid) override {
    FXL_LOG(INFO) << "VM " << vmid_ << " got online event, vmid=" << vmid;
    std::lock_guard<std::mutex> lk(state_lock_);
    for (auto& notifier : notifiers_) {
      if (notifier->peer_vmid() == vmid) {
        notifier->peer_online();
      }
    }
  }

  auto NewBinding() { return binding_.NewBinding(async_); }

  void VmOffline(int16_t vmid) override {
    std::lock_guard<std::mutex> lk(state_lock_);
    for (auto& notifier : notifiers_) {
      if (notifier->peer_vmid() == vmid) {
        notifier->peer_offline();
      }
    }
  }

  void add_notifier(PeerStateNotifier* notifier) {
    std::lock_guard<std::mutex> lk(state_lock_);
    notifiers_.push_back(notifier);
  }

 private:
  fidl::Binding<VmStateListener> binding_;
  std::mutex state_lock_;
  std::vector<PeerStateNotifier*> notifiers_;
  async_t* async_;
  int16_t vmid_;
};

}  // namespace machina