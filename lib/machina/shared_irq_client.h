// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fuchsia/cpp/machina.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest_config.h"
#include "lib/fxl/logging.h"

namespace machina {

class Guest;
class SharedIrqClient : public SharedIrqListener {
 public:
  SharedIrqClient();
  void Init(Guest* guest);

  fidl::InterfaceRequest<SharedIrqService> NewRequest() {
    return svc_.NewRequest();
  }

  static SharedIrqClient* Instance() {
    fbl::AutoLock lock(&mutex_);
    if (!instance_) {
      instance_ = std::make_unique<SharedIrqClient>();
    }
    return instance_.get();
  }

  void UpdatePreference(uint32_t irq_nr, uint32_t interested_bitmask);

 private:
  // |SharedIrqListener|
  virtual void OnNewSharedIrq(uint32_t irq_nr,
                              uint32_t interested_bitmask) override;
  virtual void OnPendingIrqEvent(uint32_t irq_nr,
                                 uint32_t pending_bitmap) override;

  Guest* guest_;
  SharedIrqServiceSyncPtr svc_;
  fidl::Binding<SharedIrqListener> listener_;
  async::Loop loop_;

  static std::unique_ptr<SharedIrqClient> instance_;
  static fbl::Mutex mutex_;
};

}  // namespace machina