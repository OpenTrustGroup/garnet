// Copyright 2018 Open Trust Group
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zx/vmar.h>

#include <fcntl.h>
#include <unistd.h>

#include "garnet/lib/gzos/trusty_virtio/shared_mem.h"

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;

namespace trusty_virtio {

zx_status_t SharedMem::Create(zx::resource& shm_rsc,
                              fbl::RefPtr<SharedMem>* out) {
  zx_info_resource_t shm_info zx_status_t status = shm_rsc.get_info(
      ZX_INFO_RESOURCE, &shm_info, sizeof(shm_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get resource info, status:" << status;
    return status;
  }

  uintptr_t base = shm_info.low;
  size_t size = shm_info.high - shm_info.low;
  zx::vmo vmo;
  status = zx::vmo::create_ns_mem(shm_rsc, base, size, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create physical vmo object, status:" << status;
    return status;
  }

  uintptr_t vaddr;
  status = zx::vmar::root_self()->map(0, vmo, 0, size, kMapFlags, &vaddr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map vmo: " << status;
    return status;
  }

  fbl::AllocChecker ac;
  *out = fbl::AdoptRef(new (&ac) SharedMem(fbl::move(vmo), base, size, vaddr));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

SharedMem::~SharedMem() {
  if (vaddr_ != 0) {
    zx::vmar::root_self()->unmap(vaddr_, vmo_size_);
  }
}

}  // namespace trusty_virtio
