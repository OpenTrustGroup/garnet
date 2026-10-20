// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/bootloader/dummy.h"

#include "lib/fxl/logging.h"

zx_status_t BootloaderDummy::Setup(
    GuestConfig& cfg,
    uintptr_t* guest_ip,
    std::vector<uint64_t>& extra_params) {
  FXL_LOG(INFO) << "DTB: " << std::hex << dtb_base_ << "-" << std::hex
                << dtb_size_;
  cfg.set_dtb(dtb_base_, dtb_size_);
  extra_params.push_back(dtb_base_);
  *guest_ip = entry_;
  return ZX_OK;
}

void BootloaderDummy::DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                                       uint32_t reason) {}
