// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_CAMDMABUF_H_
#define GARNET_LIB_MACHINA_CAMDMABUF_H_

#include <zircon/ktrace.h>
#include <zircon/nbl_trace/ktrace.h>
#include <zircon/types.h>

#include "garnet/lib/machina/guest.h"

namespace machina {

// The Singleton class defines the `GetInstance` method that serves as an
// alternative to constructor and lets clients access the same instance of this
// class over and over.
class CamDmabuf {
 public:
  // Singletons should not be cloneable.
  CamDmabuf(CamDmabuf& other) = delete;

  // Singletons should not be assignable.
  void operator=(const CamDmabuf&) = delete;

  // This is the static method that controls the access to the singleton
  // instance. On the first run, it creates a singleton object and places it
  // into the static field. On subsequent runs, it returns the client existing
  // object stored in the static field.
  static std::shared_ptr<CamDmabuf> GetInstance();

  // ATTENTION:
  //   GetInstanceNoLock can only be called in CamDmabuf()
  static std::shared_ptr<CamDmabuf> GetInstanceNoLock() { return pinstance_; }

  zx_status_t CamDmabufMemFromDtb(Guest& guest, uintptr_t guest_phys_base);
  zx_status_t MapCamDmabuf(Guest& guest, bool is_sos);
  zx_status_t PatchCamDmabufDts(Guest& guest, uintptr_t guest_phys_base);
  zx_vaddr_t GetCamDmabuf(uint32_t group);

  void SetCamDmabuf(uintptr_t paddr, size_t size) {
    camdmabuf_mem_pa_ = paddr;
    camdmabuf_mem_sz_ = size;
  }

  zx_vaddr_t buf() { return camdmabuf_mem_va_; }
  zx_paddr_t pa() { return camdmabuf_mem_pa_; }
  size_t size() { return camdmabuf_mem_sz_; }

 private:
  static std::shared_ptr<CamDmabuf> pinstance_;
  static std::mutex mutex_;

  zx::vmo CamDmabuf_vmo_;
  zx_vaddr_t camdmabuf_mem_pa_;
  size_t camdmabuf_mem_sz_;
  zx_vaddr_t camdmabuf_mem_va_;

 protected:
  // The Singleton's constructor/destructor should always be private to
  // prevent direct construction/desctruction calls with the `new`/`delete`
  // operator.
  CamDmabuf() {}
};

}  // namespace machina

#endif /* GARNET_LIB_MACHINA_UTRACE_H_ */
