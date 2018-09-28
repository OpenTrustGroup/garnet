// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_PL031_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_PL031_H_

#include <mutex>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/platform_device.h"

namespace machina {

class Guest;

// Implements the PL031 RTC.
class Pl031 : public IoHandler, public PlatformDevice {
 public:
  zx_status_t Init(Guest* guest);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // PlatformDevice interface.
  zx_status_t ConfigureDtb(void* dtb) const override;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_PL031_H_
