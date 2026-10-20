// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/reg_access.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/device/sysinfo.h>

#include "lib/fxl/logging.h"

namespace machina {

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

static zx_status_t get_root_resource(zx::resource *resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

RegAccess::~RegAccess() {
  if (vaddr_ != 0) {
    uint64_t map_size = (offset_in_vmo_ + size_ + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    zx_vmar_unmap(zx_vmar_root_self(), vaddr_, map_size);
  }
}

zx_status_t RegAccess::Init(const RegAccessSpec &spec) {
  start_paddr_ = spec.start_addr;
  size_ = spec.end_addr - spec.start_addr;

  uint64_t aligned_base = start_paddr_ & ~(PAGE_SIZE - 1);
  offset_in_vmo_ = start_paddr_ - aligned_base;
  uint64_t map_size =
      (start_paddr_ + size_ - aligned_base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  zx::resource resource;
  zx_status_t status = get_root_resource(&resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }

  status = zx_vmo_create_physical(resource.get(), aligned_base, map_size,
                                  vmo_.reset_and_get_address());
  if (status != ZX_OK)
    return status;

  vmo_.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);

  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                       0, vmo_.get(), 0, map_size, &vaddr_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "RegAccess: mapping failed: " << status;
    return status;
  }

  return guest_->CreateMapping(TrapType::MMIO_SYNC, start_paddr_, size_, 0,
                               this);
}

zx_status_t RegAccess::Read(uint64_t addr, IoValue *value) const {
  uint8_t *ptr = reinterpret_cast<uint8_t *>(vaddr_) + offset_in_vmo_ + addr;
  switch (value->access_size) {
  case 8:
    value->u64 = *(volatile uint64_t *)ptr;
    break;
  case 4:
    value->u32 = *(volatile uint32_t *)ptr;
    break;
  case 2:
    value->u16 = *(volatile uint16_t *)ptr;
    break;
  case 1:
    value->u8 = *(volatile uint8_t *)ptr;
    break;
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
  /*log for test*/
  /*FXL_LOG(INFO) << "RegAccess [SAFE READ]: PADDR=0x" << std::hex <<
     (start_paddr_ + addr)
                << " res=0x" << value->u64 << " at VM " << std::dec
     <<static_cast<int32_t>(guest_->vmid());*/

  return ZX_OK;
}

zx_status_t RegAccess::Write(uint64_t addr, const IoValue &value) {
  uint8_t *ptr = reinterpret_cast<uint8_t *>(vaddr_) + offset_in_vmo_ + addr;

  /*log for test*/
  /*FXL_LOG(INFO) << "RegAccess [SAFE WRITE]: PADDR=0x" << std::hex <<
     (start_paddr_ + addr)
                << " val=0x" << value.u64 << " at VM " << std::dec <<
     static_cast<int32_t>(guest_->vmid());*/

  switch (value.access_size) {
  case 8:
    *(volatile uint64_t *)ptr = value.u64;
    break;
  case 4:
    *(volatile uint32_t *)ptr = value.u32;
    break;
  case 2:
    *(volatile uint16_t *)ptr = value.u16;
    break;
  case 1:
    *(volatile uint8_t *)ptr = value.u8;
    break;
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t RegisterRegAccessHandlers(
    machina::Guest &guest, const std::vector<machina::RegAccessSpec> &specs,
    std::vector<std::unique_ptr<machina::RegAccess>> &handlers) {

  for (const auto &spec : specs) {
    auto handler = std::make_unique<machina::RegAccess>(&guest);
    zx_status_t status = handler->Init(spec);

    if (status == ZX_OK) {
      handlers.push_back(std::move(handler));
    } else {
      FXL_LOG(ERROR) << "RegAccess: Failed to init trap for [0x" << std::hex
                     << spec.start_addr << " - 0x" << spec.end_addr << "]";
      return status;
    }
  }

  FXL_LOG(INFO) << "RegAccess: Successfully initialized " << handlers.size()
                << " independent segment traps at VM "
                << static_cast<int32_t>(guest.vmid());

  return ZX_OK;
}

} // namespace machina
