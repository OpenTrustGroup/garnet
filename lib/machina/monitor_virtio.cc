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

#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/monitor_virtio.h"
#include "lib/fsl/handles/object_info.h"

namespace machina {

// Static methods should be defined outside the class.
std::shared_ptr<Monitor_virtio> Monitor_virtio::instance_{nullptr};
std::mutex Monitor_virtio::mutex_;
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

// The first time we call GetInstance we will lock the storage location
// and then we make sure again that the variable is null and then we
// set the value. RU:
std::shared_ptr<Monitor_virtio> Monitor_virtio::GetInstance(void) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (instance_ == nullptr)
    instance_ = std::shared_ptr<Monitor_virtio>(new Monitor_virtio());
  return instance_;
}

zx_status_t Monitor_virtio::MonitorVirtioMemFromDtb(Guest& guest,
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

  offs =
      fdt_node_offset_by_compatible(dtb, offs, "mediatek,nbl_monitor_virtio");
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  uint64_t gpaddr, gsize;
  ret = fdt_getprop_cells_u64(dtb, offs, "reg", 2, &gpaddr, &gsize);
  if (offs < 0)
    return ZX_ERR_NO_RESOURCES;

  fdt_pack(dtb);

  monitor_virtio_mem_pa_ = gpaddr;
  monitor_virtio_mem_sz_ = gsize;
  return ZX_OK;
};

zx_status_t Monitor_virtio::PatchMonitorVirtioDts(Guest& guest,
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
    offs = fdt_add_subnode(dtb, offs, "nbl_monitor_virtio");
    check_status(offs, "nbl_monitor_virtio");
    ret = fdt_setprop_string(dtb, offs, "compatible",
                             "mediatek,nbl_monitor_virtio");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, monitor_virtio_mem_pa_,
                                monitor_virtio_mem_sz_);
    check_status(ret, "reg");
    ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
    check_status(ret, "no-map");
  } else {
    offs = fdt_add_subnode(dtb, offs, "nbl_monitor_virtio");
    check_status(offs, "nbl_monitor_virtio");
    ret = fdt_setprop_string(dtb, offs, "compatible",
                             "mediatek,nbl_monitor_virtio");
    check_status(ret, "compatible");
    ret = fdt_setprop_cells_u64(dtb, offs, "reg", 2, monitor_virtio_mem_pa_,
                                monitor_virtio_mem_sz_);
    check_status(ret, "reg");
    ret = fdt_setprop(dtb, offs, "no_map", nullptr, 0);
    check_status(ret, "no-map");
  }

  fdt_pack(dtb);

  return ZX_OK;
}

zx_status_t Monitor_virtio::MapMonitorVirtioMem(Guest& guest, bool is_sos) {
  if (!monitor_virtio_mem_pa_ || !monitor_virtio_mem_sz_)
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

  status = zx_vmo_create_physical(root_resource.get(), monitor_virtio_mem_pa_,
                                  monitor_virtio_mem_sz_,
                                  vmo.reset_and_get_address());
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
    zx_vaddr_t va;
    uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                         ZX_VM_FLAG_PERM_EXECUTE | ZX_VM_FLAG_SPECIFIC |
                         ZX_VM_FLAG_MAP_CONTIGUOUS;
    // stage2 mapping for UOS
    status = zx_vmar_map(guest.vmar(), map_flags, monitor_virtio_mem_pa_,
                         vmo.get(), 0, monitor_virtio_mem_sz_, &va);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map monitor virtio physical memory 1"
                     << status;
      return status;
    }
  }

  // mapping for nebula internal use
  uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
  status = zx_vmar_map(zx_vmar_root_self(), map_flags, 0, vmo.get(), 0,
                       monitor_virtio_mem_sz_, &monitor_virtio_mem_va_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map monitor virtio physical memory 2"
                   << status;
    return status;
  }
  FXL_CHECK(vmo);
  monitor_virtio_vmo_ = std::move(vmo);
  return ZX_OK;
}

nbl_acrn_monitor_header_t* Monitor_virtio::GetMonitorHeader(uint16_t vmid) {
  if (monitor_virtio_mem_va_ == 0)
    return nullptr;
  auto header =
      reinterpret_cast<nbl_acrn_monitor_header_t*>(monitor_virtio_mem_va_);
  if (header->magic != NBL_ACRN_MONITOR_MAGIC) {
    // monitor buffer is not ready
    return nullptr;
  }
  return reinterpret_cast<nbl_acrn_monitor_header_t*>(
      (uint64_t)header + vmid * MONITOR_MEM_PER_VM);
}

int Monitor_virtio::FindAddrRangeIndex(uint64_t addr, uint16_t vmid) {
  nbl_acrn_monitor_header_t* header = GetMonitorHeader(vmid);
  struct virtio_monitor* virtio_monitor;
  int target = -1;
  int left = 0;
  int right;
  int mid;

  if (vmid >= MAX_VM_CNT) {
    printf("%s, error vmid %d\n", __func__, vmid);
    return -1;
  }

  if (header == nullptr) {
    printf("%s, monitor share memory is not ready\n", __func__);
    return -1;
  }

  virtio_monitor = header->virtio_monitor;
  right = header->acrn_device_cnt - 1;

  while (left <= right) {
    mid = left + (right - left) / 2;
    if (addr >= virtio_monitor[mid].mmio_addr &&
        addr < virtio_monitor[mid].mmio_addr + virtio_monitor[mid].mmio_size) {
      target = mid;
      break;
    } else if (addr >=
               virtio_monitor[mid].mmio_addr + virtio_monitor[mid].mmio_size) {
      left = mid + 1;
    } else if (addr < virtio_monitor[mid].mmio_addr) {
      right = mid - 1;
    }
  }
  return target;
}

struct virtio_monitor* Monitor_virtio::FindAddrRange(uint64_t addr,
                                                     uint16_t vmid) {
  nbl_acrn_monitor_header_t* header = GetMonitorHeader(vmid);
  struct virtio_monitor* virtio_monitor;
  int virtio_monitor_index;
  if (header) {
    virtio_monitor = header->virtio_monitor;
    virtio_monitor_index = FindAddrRangeIndex(addr, vmid);
    if (virtio_monitor_index > 0)
      return &virtio_monitor[virtio_monitor_index];
  }
  return nullptr;
}

}  // namespace machina
