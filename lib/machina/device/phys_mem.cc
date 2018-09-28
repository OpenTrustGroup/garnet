// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/device/phys_mem.h"

#include <lib/zx/vmar.h>

static constexpr uint32_t kMapFlags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;

namespace machina {

zx_status_t PhysMem::Init(zx::vmo vmo) {
  vmo_ = std::move(vmo);

  zx_status_t status = vmo_.get_size(&vmo_size_);
  if (status != ZX_OK) {
    return status;
  }

  return zx::vmar::root_self()->map(0, vmo_, 0, vmo_size_, kMapFlags, &addr_);
}

zx_status_t PhysMem::Init(size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, ZX_VMO_NON_RESIZABLE, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  return Init(std::move(vmo));
}

PhysMem::~PhysMem() {
  if (addr_ != 0) {
    zx::vmar::root_self()->unmap(addr_, vmo_size_);
  }
}

}  // namespace machina
