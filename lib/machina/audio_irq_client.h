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
class AudioIrqClient : public AudioIrqListener {
 public:
  AudioIrqClient();
  void Init(Guest* guest);

  fidl::InterfaceRequest<AudioIrqService> NewRequest() {
    return svc_.NewRequest();
  }

  void FastCallLoop();
  void UpdateAfeIrq(uint32_t irq_nr,
                    uint32_t irq_status_offset,
                    uint32_t irq_clear_offset,
                    uint64_t interested_bitmask,
                    uint32_t vmid,
                    uint32_t index);
  void UpdateAfeIrqRegs(uint32_t vector);

 private:
  // |AudioIrqListener|
  virtual void OnNewNoBitmaskAudioIrq(uint32_t irq_nr) override;
  virtual void OnPendingIrqEvent(uint32_t irq_nr,
                                 uint64_t pending_bitmap) override;

  Guest* guest_;
  AudioIrqServiceSyncPtr svc_;
  fidl::Binding<AudioIrqListener> listener_;
  zx::channel cb_chan_;
  async::Loop loop_;
};

}  // namespace machina