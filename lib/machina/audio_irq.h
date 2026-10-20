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
class AudioIrq {
 public:
  static void Create(zx::resource& res,
                     const AudioIrqSpecEx& spec,
                     Guest* guest,
                     std::unique_ptr<AudioIrq>* out);

  static void Create(uint16_t vector,
                     Guest* guest,
                     std::unique_ptr<AudioIrq>* out);

  AudioIrq(zx::interrupt interrupt,
           zx::vmo vmo,
           const AudioIrqSpecEx& spec,
           Guest* guest);
  AudioIrq(uint16_t vector, Guest* guest);
  ~AudioIrq();

  uint32_t vector() { return spec_.vector; }

  uint32_t GetIrqStatus(uint32_t vmid);
  void AppendIrqStatus(uint32_t vmid, uint64_t bitmask);
  void ClearIrqStatus(uint32_t vmid, uint32_t bitmask);

  void set_afe_irq_spec(uint32_t irq_status_offset,
                        uint32_t irq_clear_offset,
                        uint64_t bitmask,
                        uint32_t vmid,
                        uint32_t index);
  void update_afe_irq_regs_map();
  const AudioIrqSpecEx& GetAudioIrqSpecEx() const { return spec_; }

 private:
  uint32_t reg_read32(uint32_t offset);
  void reg_write32(uint32_t offset, uint32_t bitmask);
  uint32_t GetAfeIRQIndex(uint32_t clear_reg);

  void HostTriggerVirqLocked() __TA_REQUIRES(mutex_);
  void GuestTriggerVirqLocked(uint32_t bitmask, uint32_t vmid)
      __TA_REQUIRES(mutex_);

  zx::interrupt interrupt_;
  zx::vmo ctrl_regs_vmo_;

  uint8_t* mapped_ctrl_regs_ = nullptr;
  std::unordered_map<uint32_t, uint32_t> vm_irq_status_ __TA_GUARDED(mutex_);
  AudioIrqSpecEx spec_ = {};

  std::unique_ptr<std::thread> thread_;
  bool should_stop_ = false;

  Guest* guest_ = nullptr;
  fbl::Mutex mutex_;
  //  std::atomic<uint8_t> last_vcpu_;
};

}  // namespace machina
