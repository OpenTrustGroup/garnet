// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_

#include <fbl/mutex.h>
#include <fbl/atomic.h>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/vmlog_store/vmlog_store.h"

namespace machina {

class Guest;

// Implements the PL011 UART.
class Pl011 : public IoHandler {
 public:
  zx_status_t Init(Guest* guest, uint64_t addr, int32_t vmid,
                   VmlogStore* logstore);
  zx_status_t Init(Guest* guest, uint64_t addr, VmlogStore* logstore);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  int32_t vmid_ = -1;
  static constexpr size_t kBufferSize = 200;
  mutable fbl::Mutex mutex_;

  uint8_t tx_buffer_[kBufferSize + 1] __TA_GUARDED(mutex_) = {};
  uint8_t tx_buf_with_prefix_[kBufferSize + 16] __TA_GUARDED(mutex_) = {};
  uint16_t tx_offset_ __TA_GUARDED(mutex_) = 0;

  uint32_t control_ __TA_GUARDED(mutex_) = 0;

  void Print(uint8_t ch);
  void WriteDlogLocked(void) __TA_REQUIRES(mutex_);

  VmlogStore* logstore_;
  fbl::atomic<bool> log_sinking_{false};
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_
