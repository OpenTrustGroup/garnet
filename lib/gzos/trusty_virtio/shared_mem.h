// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zircon/syscalls/smc_service.h>
#include <zx/vmo.h>
#include <type_traits>

#include "garnet/public/lib/fxl/logging.h"

namespace trusty_virtio {

static bool validate_range(uintptr_t addr,
                           size_t size,
                           uintptr_t mem_addr,
                           size_t mem_size) {
  uintptr_t range_end = addr + size;
  uintptr_t mem_end = mem_addr + mem_size;

  return addr >= mem_addr && range_end <= mem_end;
}

class SharedMem : public fbl::RefCounted<SharedMem> {
 public:

  static zx_status_t Create(zx::vmo vmo, zx_info_ns_shm_t vmo_info, fbl::RefPtr<SharedMem>* out);
  ~SharedMem();

  uintptr_t VirtToPhys(void* addr, size_t size) {
    if (validate_vaddr_range(addr, size))
      return paddr_ + (reinterpret_cast<uintptr_t>(addr) - vaddr_);
    else
      return 0;
  }

  template <typename T>
  T* PhysToVirt(uintptr_t addr, size_t size) {
    if (validate_paddr_range(addr, size))
      return reinterpret_cast<T*>(vaddr_ + (addr - paddr_));
    else
      return nullptr;
  }

  bool validate_vaddr_range(void* addr, size_t size) {
    return validate_range(reinterpret_cast<uintptr_t>(addr), size, vaddr_,
                          vmo_size_);
  }

  bool validate_paddr_range(zx_paddr_t addr, size_t size) {
    return validate_range(addr, size, paddr_, vmo_size_);
  }

  uintptr_t addr() const { return vaddr_; }
  size_t size() const { return vmo_size_; }
  bool use_cache() const { return use_cache_; }

  template <typename T>
  T* as(uintptr_t off) const {
    FXL_DCHECK(off + sizeof(T) <= vmo_size_)
        << "Region is outside of shared memory";
    return reinterpret_cast<T*>(vaddr_ + off);
  }

  void* ptr(uintptr_t off, size_t len) const {
    FXL_DCHECK(off + len <= vmo_size_) << "Region is outside of shared memory";
    return reinterpret_cast<void*>(vaddr_ + off);
  }

 private:
  SharedMem(zx::vmo vmo, zx_info_ns_shm_t vmo_info, uintptr_t vaddr)
      : vmo_(fbl::move(vmo)),
        vmo_size_(vmo_info.size),
        vaddr_(vaddr),
        paddr_(vmo_info.base_phys),
        use_cache_(vmo_info.use_cache) {}

 protected:
  zx::vmo vmo_;
  size_t vmo_size_;
  uintptr_t vaddr_;
  uintptr_t paddr_;
  bool use_cache_;
};

}  // namespace trusty_virtio
