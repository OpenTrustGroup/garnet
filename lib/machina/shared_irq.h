// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest_config.h"
#include "lib/fxl/logging.h"
#include "lib/zx/interrupt.h"
#include "lib/zx/vmo.h"

namespace machina {

class Guest;
class SharedIrq {
 public:
  static void Create(zx::resource& res,
                     const SharedIrqSpec& spec,
                     Guest* guest,
                     std::unique_ptr<SharedIrq>* out);

  static void Create(uint16_t vector,
                     uint32_t interested_bitmask,
                     Guest* guest,
                     std::unique_ptr<SharedIrq>* out);

  SharedIrq(zx::interrupt interrupt,
            zx::vmo vmo,
            const SharedIrqSpec& spec,
            Guest* guest);
  SharedIrq(uint16_t vector, uint32_t interested_bitmask, Guest* guest);
  ~SharedIrq();

  uint32_t vector() { return spec_.vector; }

  uint32_t GetIrqStatus();
  void AppendIrqStatus(uint32_t bitmask);
  void ClearIrqStatus(uint32_t bitmask);

  void set_interest_bitmap(uint32_t bitmask);

 private:
  uint32_t reg_read32(uint32_t offset);
  void reg_write32(uint32_t offset, uint32_t bitmask);

  void MaybeTriggerVirqLocked(bool bHost = true) __TA_REQUIRES(mutex_);
  void MaybeForwardIrqEventLocked() __TA_REQUIRES(mutex_);

  zx::interrupt interrupt_;
  zx::vmo ctrl_regs_vmo_;

  uint8_t* mapped_ctrl_regs_ = nullptr;
  uint32_t irq_status_ __TA_GUARDED(mutex_) = 0x0;
  SharedIrqSpec spec_ = {};

  std::unique_ptr<std::thread> thread_;
  bool should_stop_ = false;

  Guest* guest_ = nullptr;
  fbl::Mutex mutex_;
};

}  // namespace machina
