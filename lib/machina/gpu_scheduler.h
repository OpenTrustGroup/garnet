// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <fuchsia/cpp/machina.h>
#include <lib/fidl/cpp/binding.h>
#include <zx/handle.h>

#include "lib/svc/cpp/service_provider_bridge.h"

#define JOB_IRQ_INX 0
#define MMU_IRQ_INX 1
#define GPU_IRQ_INX 2
#define EVT_IRQ_INX 3
#define PWR_IRQ_INX 4
#define MAX_GPU_IRQ 5

#define GRANT_IRQ_INX 0
#define STOP_IRQ_INX 1
#define LOST_IRQ_INX 2
#define RCVR_IRQ_INX 3
#define MAX_NOTIFY_IRQ 4

#define SCHEDULE_TIMER_INX 0
#define STOPPING_TIMER_INX 1
#define RECOVERY_TIMER_INX 2
#define PREPARING_TIMER_INX 3
#define MAX_VM_TIMER 4

struct gpu_shed_param {
  zx_handle_t resource;
  uint32_t notify_irq[MAX_NOTIFY_IRQ];
  uint32_t gpu_irq[MAX_GPU_IRQ];
  std::vector<uint64_t> gpu_regions;
  uint64_t* guest_ipa;
  uint32_t* ipa_size;
  uint64_t sos_timer[MAX_VM_TIMER];
  uint64_t uos_timer[MAX_VM_TIMER];
  bool gpu_reserved_mem;
  uint8_t sched_mode;
  uint64_t* guest_aee_ipa;
  size_t* guest_aee_size;
};

namespace machina {
class Guest;

class GpuSchedulerServiceImpl : public GpuScheduleService {
 public:
  GpuSchedulerServiceImpl(component::ServiceProviderBridge* bridge,
                          Guest* guest);
  void SetVmid(uint32_t vmid) { vmid_ = vmid; }
  void GetSchedHandle(GetSchedHandleCallback callback);
  void GetSchedIrq(GetSchedIrqCallback callback);
  void GetSchedReg(GetSchedRegCallback callback);
  void GetSchedTime(GetSchedTimeCallback callback);
  void GetSchedRsv(GetSchedRsvCallback callback);
  void GetSchedAeeInfo(GetSchedAeeInfoCallback callback);
  zx_status_t createGpuSched(gpu_shed_param& param);
  zx_status_t destroyGpuSched(uint32_t vmid);
  zx_status_t addVcpu(zx_handle_t vcpu, bool affinity);
  zx_status_t GetGpuebShareMemFromDtb(void* dtb, size_t dtb_size);
  zx_status_t SetGpuebShareMemToKernel(gpu_shed_param& param);

 private:
  zx_status_t createSharedMem(uint64_t& guest_ipa, uint32_t& size);
  void PublishService();
  bool IsSchedReady() const {
    return sched_handle_ && sched_handle_.get() != ZX_HANDLE_INVALID;
  }

  component::ServiceProviderBridge* bridge_ = nullptr;
  fidl::BindingSet<GpuScheduleService> bindings_;
  Guest* guest_;
  zx::handle sched_handle_;
  uint32_t vmid_;
  uint32_t notify_irq_[MAX_NOTIFY_IRQ];
  uint32_t gpu_irq_[MAX_GPU_IRQ];
  zx::vmo share_vmo_;
  std::vector<uint64_t> gpu_regions_;
  std::vector<uint64_t> sos_timer_period_;
  std::vector<uint64_t> uos_timer_period_;
  bool gpu_reserved_mem_;
  zx::vmo shared_aee_vmo_;
  uint64_t guest_aee_ipa_;
  size_t guest_aee_size_;
  zx_paddr_t gpueb_shmem_paddr_ = 0;
  size_t gpueb_shmem_size_ = 0;
  bool service_published_ = false;
};

class GpuSchedulerClient {
 public:
  GpuSchedulerClient(Guest* guest);
  void SetVmid(uint32_t vmid) { vmid_ = vmid; }
  fidl::InterfaceRequest<GpuScheduleService> NewRequest() {
    return svc_.NewRequest();
  }
  zx_status_t prepareGpuSched(zx_handle_t resource);
  zx_status_t addVcpu(zx_handle_t vcpu, bool affinity);
  void GetGpuSchedInfo(uint64_t& guest_ipa,
                       uint32_t& ipa_size,
                       uint32_t notify_irq[],
                       uint32_t gpu_irq[],
                       std::vector<uint64_t>& gpu_regions,
                       bool& gpu_reserved_mem);
  void GetGpuSchedAeeInfo(uint64_t& guest_aee_ipa, size_t& guest_aee_size);

 private:
  GpuScheduleServiceSyncPtr svc_;
  Guest* guest_;
  zx::handle sched_handle_;
  uint32_t vmid_;
  zx::vmo share_vmo_;
  uint64_t guest_ipa_;
  uint32_t ipa_size_;
  uint64_t guest_aee_ipa_;
  size_t guest_aee_size_;
  uint32_t notify_irq_[MAX_GPU_IRQ];
  uint32_t gpu_irq_[MAX_GPU_IRQ];
  std::vector<uint64_t> gpu_regions_;
  bool gpu_reserved_mem_;
};
}  // namespace machina
