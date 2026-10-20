// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_UTRACE_H_
#define GARNET_LIB_MACHINA_UTRACE_H_

#include <zircon/types.h>
#include <zircon/ktrace.h>
#include <zircon/nbl_trace/ktrace.h>

#include "garnet/lib/machina/guest.h"

#define NBL_TRACE_MIN_RESERVE_MEM    0x10000

typedef struct {
  uint64_t magic;
#define NBL_SCHED_TRACE_MAGIC 0x5472617363686564  // Trasched
  std::atomic<uint64_t> write_pos;
  volatile uint64_t read_pos;
#define KTRACE_MODE_FIFO 0
#define KTRACE_MODE_SNAPSHOT 1
  uint32_t mode : 1;
  uint32_t sz : 31;
  std::atomic<uint32_t> enable;
  // events we supported
  uint32_t support_events;
  uint32_t set_events;
  char events_name[TRACE_EVENT_CNT][EVENT_NAME_LEN_MAX];
  uint32_t state;
  uint32_t grpmask;
  uint32_t meta_len;
  std::atomic<uint32_t> meta_write_pos;
  uint32_t meta_update_cnt;
  uint32_t reserved[28];
  volatile char data[];
} ktrace_ringbuf_t;

namespace machina {

// The Singleton class defines the `GetInstance` method that serves as an
// alternative to constructor and lets clients access the same instance of this
// class over and over.
class Utrace {
 public:
  // Singletons should not be cloneable.
  Utrace(Utrace& other) = delete;

  // Singletons should not be assignable.
  void operator=(const Utrace&) = delete;

  // This is the static method that controls the access to the singleton
  // instance. On the first run, it creates a singleton object and places it
  // into the static field. On subsequent runs, it returns the client existing
  // object stored in the static field.
  static std::shared_ptr<Utrace> GetInstance();

  // ATTENTION:
  //   GetInstanceNoLock can only be called in utrace()
  static std::shared_ptr<Utrace> GetInstanceNoLock() { return pinstance_; }

  zx_status_t TraceMemFromDtb(Guest& guest, uintptr_t guest_phys_base, uint8_t enable);
  zx_status_t MapTraceBuf(Guest& guest, bool is_sos);
  zx_status_t PatchTraceDts(Guest& guest, uintptr_t guest_phys_base);
  zx_status_t SetTraceBufToKernel();
  zx_status_t SetTraceBufToCpuFreq();
  zx_vaddr_t GetTraceBuf(uint32_t group);

  void SetTraceMem(uintptr_t paddr, size_t size) {
    trace_mem_pa_ = paddr;
    trace_mem_sz_ = size;
  }

  const zx::vmo& vmo() { return trace_vmo_; }
  uintptr_t buf() { return trace_mem_va_; }
  zx_paddr_t pa() { return trace_mem_pa_; }
  size_t size() { return trace_mem_sz_; }

 private:
  static std::shared_ptr<Utrace> pinstance_;
  static std::mutex mutex_;

  zx::vmo trace_vmo_;
  uintptr_t trace_mem_pa_ = 0;
  size_t trace_mem_sz_ = 0;
  zx_vaddr_t trace_mem_va_ = 0;

 protected:
  // The Singleton's constructor/destructor should always be private to
  // prevent direct construction/desctruction calls with the `new`/`delete`
  // operator.
  Utrace() {}
};

}  // namespace machina

void utrace(uint32_t tag,
            uint8_t meta_category,
            uint8_t meta_event,
            uint8_t meta_a = 0,
            uint64_t a = 0,
            uint8_t meta_b = 0,
            uint64_t b = 0,
            uint8_t meta_c = 0,
            uint64_t c = 0,
            uint8_t meta_d = 0,
            uint64_t d = 0);

#endif /* GARNET_LIB_MACHINA_UTRACE_H_ */
