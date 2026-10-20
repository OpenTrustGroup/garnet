// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_MONITOR_VIRTIO_H_
#define GARNET_LIB_MACHINA_MONITOR_VIRTIO_H_

#include <zircon/ktrace.h>
#include <zircon/types.h>

#include "garnet/lib/machina/guest.h"

#define NBL_ACRN_MONITOR_MAGIC 0x19228389AFED12DE  // NblAcrnMonitor

#define GRP_ACRN_DEV_CNT 300
#define MAX_VM_CNT 2
#define MONITOR_MEM_PER_VM (4096 * 8)
#define ACRN_MONITOR_TIMEROUT 3  // 3*20ms

enum { STAGE_ANDROID = 0, STAGE_UOS, STAGE_SOS, STAGE_ACRN_DIRVER, STAGE_SUM };

struct virtio_monitor {
  uint64_t mmio_addr;
  uint64_t mmio_size;
  uint64_t time;
  uint64_t last_time;
  uint64_t time_out;
  // atomic
  uint64_t cnt;
  uint8_t cur_stage;
  uint8_t last_cur_stage;
  uint8_t trap_type;
  uint8_t timeout_cnt;
} __attribute__((aligned(64)));

typedef struct {
  uint64_t magic;
  uint32_t version;
  uint32_t acrn_device_cnt;
  struct virtio_monitor virtio_monitor[GRP_ACRN_DEV_CNT];
} nbl_acrn_monitor_header_t;

namespace machina {

class Monitor_virtio {
 public:
  // Singletons should not be cloneable.
  Monitor_virtio(Monitor_virtio& other) = delete;

  // Singletons should not be assignable.
  void operator=(const Monitor_virtio&) = delete;

  // This is the static method that controls the access to the singleton
  // instance. On the first run, it creates a singleton object and places it
  // into the static field. On subsequent runs, it returns the client existing
  // object stored in the static field.
  static std::shared_ptr<Monitor_virtio> GetInstance();

  // ATTENTION:
  //   GetInstanceNoLock can only be called in utrace()
  static std::shared_ptr<Monitor_virtio> GetInstanceNoLock() {
    return instance_;
  }

  zx_status_t MonitorVirtioMemFromDtb(Guest& guest, uintptr_t guest_phys_base);
  zx_status_t MapMonitorVirtioMem(Guest& guest, bool is_sos);
  zx_status_t PatchMonitorVirtioDts(Guest& guest, uintptr_t guest_phys_base);
  nbl_acrn_monitor_header_t* GetMonitorHeader(uint16_t vmid);
  int FindAddrRangeIndex(uint64_t addr, uint16_t vmid);
  struct virtio_monitor* FindAddrRange(uint64_t addr, uint16_t vmid);

  void SetMonitorVirtioMem(uintptr_t paddr, size_t size) {
    monitor_virtio_mem_pa_ = paddr;
    monitor_virtio_mem_sz_ = size;
  }

  uintptr_t mem() { return monitor_virtio_mem_va_; }
  zx_paddr_t pa() { return monitor_virtio_mem_pa_; }
  size_t size() { return monitor_virtio_mem_sz_; }

 protected:
  // The Singleton's constructor/destructor should always be private to
  // prevent direct construction/desctruction calls with the `new`/`delete`
  // operator.
  Monitor_virtio() {}

 private:
  static std::shared_ptr<Monitor_virtio> instance_;
  static std::mutex mutex_;

  zx::vmo monitor_virtio_vmo_;
  uintptr_t monitor_virtio_mem_pa_;
  size_t monitor_virtio_mem_sz_;
  zx_vaddr_t monitor_virtio_mem_va_ = 0;
};

}  // namespace machina

#endif /* GARNET_LIB_MACHINA_MONITOR_VIRTIO_H_ */
