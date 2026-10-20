// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_BOOTLOADER_DUMMY_H_
#define GARNET_BIN_GUEST_BOOTLOADER_DUMMY_H_

#include "garnet/bin/guest/bootloader/bootloader.h"

class BootloaderDummy : public Bootloader {
 public:
  BootloaderDummy(nbl_vmm::BootloaderRecord& rec, machina::Guest& guest)
      : Bootloader(rec, guest),
        dtb_base_(rec.dtb_base()),
        dtb_size_(rec.dtb_size()),
        entry_(rec.entry()) {}

 protected:
  zx_status_t Setup(GuestConfig& cfg,
                    uintptr_t* guest_ip,
                    std::vector<uint64_t>& extra_params) override;

  void DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                        uint32_t reason) override;

 private:
  uint64_t dtb_base_;
  uint64_t dtb_size_;
  uint64_t entry_;
};

#endif  // GARNET_BIN_GUEST_BOOTLOADER_DUMMY_H_
