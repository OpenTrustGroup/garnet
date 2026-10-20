// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PHYS_MEM_H_
#define GARNET_LIB_MACHINA_PHYS_MEM_H_

#include <lib/zx/vmo.h>
#include <type_traits>

#include "lib/fxl/logging.h"

namespace machina {

class PhysMem {
 public:
  zx_status_t Init(size_t mem_size);
  zx_status_t Init(zx::vmo vmo, uint64_t phys_base);
  zx_status_t UnmapPhysicalMemory(uintptr_t gpaddr, size_t length);

  ~PhysMem();

  const zx::vmo& vmo() const { return vmo_; }
  uintptr_t addr() const { return addr_; }
  uint64_t phys_base() const { return phys_base_; }
  size_t size() const { return vmo_size_; }

  bool contains(uintptr_t paddr, size_t len) {
    auto off = paddr - addr_;
    return off + len <= vmo_size_;
  }

  bool contains_gpa(uintptr_t paddr, size_t len) const {
    return paddr >= phys_base_ && len <= vmo_size_ &&
           paddr - phys_base_ <= vmo_size_ - len;
  }

  template <typename T>
  T* as(uintptr_t off, size_t len = sizeof(T)) const {
    FXL_DCHECK(off + len <= vmo_size_)
        << "Region is outside of guest physical memory";
    return reinterpret_cast<T*>(addr_ + off);
  }

  template <typename T>
  uintptr_t offset(T* ptr, size_t size = sizeof(T)) const {
    uintptr_t off = reinterpret_cast<uintptr_t>(ptr);
    FXL_DCHECK(off >= addr_ && (off - addr_ + size <= vmo_size_))
        << "Pointer is not contained within guest physical memory";
    return off - addr_;
  }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  uintptr_t addr_ = 0;
  uint64_t phys_base_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PHYS_MEM_H_
