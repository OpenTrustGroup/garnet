// Copyright 2018 OpenTrustGroup. All rights reserved.
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

zx_status_t SharedMem::Create(zx::vmo vmo, zx_info_ns_shm_t vmo_info, fbl::RefPtr<SharedMem>* out) {
  uintptr_t vaddr;
  zx_status_t status =
      zx::vmar::root_self().map(0, vmo, 0, vmo_info.size, kMapFlags, &vaddr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map vmo: " << status;
    return status;
  }

  fbl::AllocChecker ac;
  *out = fbl::AdoptRef(new (&ac) SharedMem(fbl::move(vmo), vmo_info, vaddr));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

SharedMem::~SharedMem() {
  if (vaddr_ != 0) {
    zx::vmar::root_self().unmap(vaddr_, vmo_size_);
  }
}

}  // namespace trusty_virtio
