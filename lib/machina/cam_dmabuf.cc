// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <atomic>
#include <vector>

#include <iostream>
#include <thread>

#include <acrn/atomic.h>

#include <libfdt.h>
#include <zircon/device/ktrace.h>
#include <zircon/device/sysinfo.h>
#include <zircon/nbl_trace/nbl_trace.h>
#include <zircon/syscalls.h>

#include "garnet/lib/machina/cam_dmabuf.h"
#include "garnet/lib/machina/fdt_utils.h"
#include "lib/fsl/handles/object_info.h"

#define DUMMY_EVT 0xfff

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

namespace machina {

// static constexpr char kKtraceDevPath[] = "/dev/misc/ktrace";

// Static methods should be defined outside the class.
std::shared_ptr<CamDmabuf> CamDmabuf::pinstance_{nullptr};
std::mutex CamDmabuf::mutex_;

// The first time we call GetInstance we will lock the storage location
// and then we make sure again that the variable is null and then we
// set the value. RU:
std::shared_ptr<CamDmabuf> CamDmabuf::GetInstance(void) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pinstance_ == nullptr)
    pinstance_ = std::shared_ptr<CamDmabuf>(new CamDmabuf());
  return pinstance_;
}

zx_status_t CamDmabuf::MapCamDmabuf(Guest& guest, bool is_sos) {
  if (!camdmabuf_mem_pa_ || !camdmabuf_mem_sz_)
    return ZX_ERR_NOT_FOUND;

  if (guest.vmar() == ZX_HANDLE_INVALID)
    return ZX_ERR_INVALID_ARGS;

  zx::vmo vmo;
  zx::resource root_resource;

  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get root resource " << status;
    return status;
  }

  status =
      zx_vmo_create_physical(root_resource.get(), camdmabuf_mem_pa_,
                             camdmabuf_mem_sz_, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
    return status;
  }

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
    return status;
  }

  if (!is_sos) {
    zx_paddr_t pa;
    uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                         ZX_VM_FLAG_PERM_EXECUTE | ZX_VM_FLAG_SPECIFIC |
                         ZX_VM_FLAG_MAP_CONTIGUOUS;
    // stage2 mapping for UOS
    status = zx_vmar_map(guest.vmar(), map_flags, camdmabuf_mem_pa_, vmo.get(),
                         0, camdmabuf_mem_sz_, &pa);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map trace physical memory " << status;
      return status;
    }
  }

  // mapping for nebula internal use
  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0,
                       vmo.get(), 0, camdmabuf_mem_sz_, &camdmabuf_mem_va_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map trace physical memory " << status;
    return status;
  }

  FXL_CHECK(vmo);
  CamDmabuf_vmo_ = std::move(vmo);

  return ZX_OK;
}

zx_status_t CamDmabuf::PatchCamDmabufDts(Guest& guest,
                                         uintptr_t guest_phys_base) {
  uintptr_t dtb_offset = guest.dtb_spec().base - guest_phys_base;
  size_t dtb_size = guest.dtb_spec().size;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0) {
    FXL_LOG(INFO) << "can not find /reserved-memory, now create a new node";

    offs = fdt_add_subnode(dtb, 0, "reserved-memory");
    check_status(offs, "reserved-memory");
    ret = fdt_setprop(dtb, offs, "ranges", nullptr, 0);
    check_status(ret, "ranges");
    fdt_setprop_cell(dtb, offs, "#size-cells", 0x02);
    fdt_setprop_cell(dtb, offs, "#address-cells", 0x02);

    offs = fdt_path_offset(dtb, "/reserved-memory");
    offs = fdt_add_subnode(dtb, offs, "grt_dmabuf_heap");
    check_status(offs, "grt_dmabuf_heap");
    ret =
        fdt_setprop_string(dtb, offs, "compatible", "mediatek,grt_dmabuf_heap");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, camdmabuf_mem_pa_,
                                camdmabuf_mem_sz_);
    check_status(ret, "reg");
    ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
    check_status(ret, "no-map");
  } else {
    offs = fdt_add_subnode(dtb, offs, "grt_dmabuf_heap");
    check_status(offs, "grt_dmabuf_heap");
    ret =
        fdt_setprop_string(dtb, offs, "compatible", "mediatek,grt_dmabuf_heap");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, camdmabuf_mem_pa_,
                                camdmabuf_mem_sz_);
    check_status(ret, "reg");
    ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
    check_status(ret, "no-map");
  }

  fdt_pack(dtb);

  return ZX_OK;
}

zx_status_t CamDmabuf::CamDmabufMemFromDtb(Guest& guest,
                                           uintptr_t guest_phys_base) {
  auto dtb_spec = guest.dtb_spec();
  uintptr_t dtb_offset = dtb_spec.base - guest_phys_base;
  size_t dtb_size = dtb_spec.size;

  // Validate device tree.
  void* dtb = guest.phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  offs = fdt_node_offset_by_compatible(dtb, offs, "mediatek,grt_dmabuf_heap");
  if (offs < 0) {
    camdmabuf_mem_pa_ = 0;
    camdmabuf_mem_sz_ = 0;
    return ZX_ERR_NO_RESOURCES;
  }
  uint64_t gpaddr, gsize;
  ret = fdt_getprop_cells_u64(dtb, offs, "reg", 2, &gpaddr, &gsize);
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  fdt_pack(dtb);

  camdmabuf_mem_pa_ = gpaddr;
  camdmabuf_mem_sz_ = gsize;
  return ZX_OK;
};
}  // namespace machina
