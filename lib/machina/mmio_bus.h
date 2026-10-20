// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <fbl/mutex.h>

#include "guest.h"
#include "interrupt_controller.h"
#include "virtio_mmio.h"

#define MMIO_MAX_DEVICES 1u

namespace machina {

constexpr uint64_t kMmioPhysStart = 0xa00000000UL;
constexpr uint64_t kMmioSize = 0x1000;

class MmioBus {
 public:
  MmioBus(Guest* guest, InterruptController* interrupt_controller,
          std::vector<uint32_t> const& mmio_irqs)
      : interrupt_controller_(interrupt_controller), mmio_irqs_(mmio_irqs),
        guest_(guest) {}

  MmioBus(Guest* guest, InterruptController* interrupt_controller,
          uint64_t irq_start)
      : interrupt_controller_(interrupt_controller), guest_(guest) {
    for (int i = 0; i < MMIO_MAX_DEVICES; ++i) {
      mmio_irqs_.push_back(irq_start++);
    }
  }
  zx_status_t Connect(MmioDevice* device) __TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t Interrupt(MmioDevice& device);

 private:
  mutable fbl::Mutex mutex_;

  MmioDevice* device_[MMIO_MAX_DEVICES] = {};
  InterruptController* interrupt_controller_ = nullptr;
  size_t next_open_slot_ = 0;
  uint64_t next_mmio_addr_ = kMmioPhysStart;
  std::vector<uint32_t> mmio_irqs_;
  Guest* guest_;
};

} // namespace machina
