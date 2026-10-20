// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/bootloader/bootloader.h"

class BootloaderLinux : public Bootloader {
 public:
  BootloaderLinux(nbl_vmm::BootloaderRecord& rec, machina::Guest& guest)
      : Bootloader(rec, guest) {}

 protected:
  virtual zx_status_t Setup(
      GuestConfig& cfg,
      uintptr_t* guest_ip,
      std::vector<uint64_t>& extra_params) override;

  virtual void DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                                uint32_t reason) override;
};
