// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/phys_mem.h"

#include <unistd.h>
#include <fcntl.h>

#include <lib/zx/vmar.h>
#include <lib/zx/resource.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

namespace machina {

zx_status_t PhysMem::Init(size_t size) {
  zx_status_t status = zx::vmo::create(size, 0, &vmo_);
  if (status != ZX_OK)
    return status;

  status = zx::vmar::root_self().map(0, vmo_, 0, size, kMapFlags, &addr_);
  if (status != ZX_OK) {
    vmo_.reset();
    return status;
  }

  vmo_size_ = size;
  return ZX_OK;
}

zx_status_t PhysMem::Init(zx::vmo vmo, uint64_t phys_base) {
  phys_base_ = phys_base;
  vmo_ = std::move(vmo);
  zx_status_t status = vmo_.get_size(&vmo_size_);
  if (status != ZX_OK) {
    return status;
  }

  return zx::vmar::root_self().map(0, vmo_, 0, vmo_size_, kMapFlags, &addr_);
}

zx_status_t PhysMem::UnmapPhysicalMemory(uintptr_t paddr, size_t length) {
  auto off = paddr - phys_base_;
  return zx::vmar::root_self().unmap(addr_ + off, length);
}

PhysMem::~PhysMem() {
  if (addr_ != 0) {
    zx::vmar::root_self().unmap(addr_, vmo_size_);
  }
}

}  // namespace machina
