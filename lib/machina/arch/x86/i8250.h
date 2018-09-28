// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_I8250_H_
#define GARNET_LIB_MACHINA_ARCH_X86_I8250_H_

#include <mutex>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/platform_device.h"

namespace machina {

class Guest;

// Implements the I8250 UART.
class I8250 : public IoHandler {
 public:
  zx_status_t Init(Guest* guest, uint64_t addr);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* io) const override;
  zx_status_t Write(uint64_t addr, const IoValue& io) override;

 private:
  static constexpr size_t kBufferSize = 128;
  mutable std::mutex mutex_;

  uint8_t tx_buffer_[kBufferSize] = {};
  uint16_t tx_offset_ = 0;

  uint8_t interrupt_enable_ __TA_GUARDED(mutex_) = 0;
  uint8_t line_control_ __TA_GUARDED(mutex_) = 0;

  void Print(uint8_t ch);
};

class I8250Group : public PlatformDevice {
 public:
  zx_status_t Init(Guest* guest);

 private:
  static constexpr size_t kNumUarts = 4;
  I8250 uarts_[kNumUarts];
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_I8250_H_
