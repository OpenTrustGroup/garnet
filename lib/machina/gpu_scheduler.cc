// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <cstdarg>
#include <vector>

#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>

#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/guest.h"
#include "gpu_scheduler.h"

namespace machina {

constexpr size_t kShareMemBase = 0x88000b000;
constexpr size_t kSharedMemSize = 4096;
static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC |
    ZX_VM_FLAG_PERM_EXECUTE;

GpuSchedulerServiceImpl::GpuSchedulerServiceImpl(
    component::ServiceProviderBridge* bridge,
    Guest* guest)
    : bridge_(bridge), guest_(guest) {}

void GpuSchedulerServiceImpl::PublishService() {
  if (service_published_ || bridge_ == nullptr) {
    return;
  }
  bridge_->AddService<GpuScheduleService>(
      [this](fidl::InterfaceRequest<GpuScheduleService> request) {
        if (!IsSchedReady()) {
          FXL_LOG(ERROR) << "Reject GpuScheduleService bind: scheduler not ready";
          return;
        }
        bindings_.AddBinding(this, std::move(request));
      });
  service_published_ = true;
}

zx_status_t GpuSchedulerServiceImpl::createGpuSched(gpu_shed_param& param) {
  zx_status_t status;
  zx_handle_t hnd;
  uint64_t addr;
  uint64_t aee_addr = *param.guest_aee_ipa;
  size_t aee_size = *param.guest_aee_size;
  zx::vmo shared_vmo;
  zx::vmo shared_aee_vmo;
  zx::handle sched_handle(0);

  status = zx::vmo::create(kSharedMemSize, 0, &shared_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create shared memory:" << status;
    return status;
  }

  if (aee_size > 0) {
    FXL_LOG(INFO) << "[swa-aee]createGpuSched create shared aee physical vmo("
                  << aee_addr << ", " << aee_size << ")...\n";
    status = zx_vmo_create_physical(param.resource, aee_addr, aee_size,
                                    shared_aee_vmo.reset_and_get_address());
    if (status != ZX_OK) {
      FXL_LOG(ERROR)
          << "[swa-aee]createGpuSched failed to create shared aee physical vmo:"
          << status;
      return status;
    }
  }

  status =
      zx::unowned_vmar::wrap(guest_->vmar())
          .map(kShareMemBase, shared_vmo, 0, kSharedMemSize, kMapFlags, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map shared memory:" << status;
    return status;
  }

  status = zx_gpu_scheduler_create(
      param.resource, param.gpu_irq, MAX_GPU_IRQ, param.notify_irq,
      MAX_NOTIFY_IRQ, param.gpu_regions.data(), param.gpu_regions.size(), &hnd);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create gpu schedule:" << status;
    goto unmap_ipa;
  }

  sched_handle.reset(hnd);
  status = zx_gpu_scheduler_set_vmo(hnd, vmid_, shared_vmo.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set vmo:" << status;
    goto unmap_ipa;
  }

  if (aee_size > 0) {
    FXL_LOG(INFO) << "[swa-aee]createGpuSched set shared aee memory...\n";
    status = zx_gpu_scheduler_set_aee_vmo(hnd, shared_aee_vmo.get());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "[swa-aee]Failed to set shared aee memory:" << status;
      goto unmap_ipa;
    }
    FXL_LOG(INFO) << "[swa-aee]createGpuSched set shared aee memory done\n";
  }

  sos_timer_period_ = {param.sos_timer[SCHEDULE_TIMER_INX],
                       param.sos_timer[STOPPING_TIMER_INX],
                       param.sos_timer[RECOVERY_TIMER_INX],
                       param.sos_timer[PREPARING_TIMER_INX]};
  uos_timer_period_ = {param.uos_timer[SCHEDULE_TIMER_INX],
                       param.uos_timer[STOPPING_TIMER_INX],
                       param.uos_timer[RECOVERY_TIMER_INX],
                       param.uos_timer[PREPARING_TIMER_INX]};

  status = zx_gpu_scheduler_set_mode(hnd, param.sched_mode);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set mode:" << status;
    goto unmap_ipa;
  }

  status = zx_gpu_scheduler_set_timeslice(hnd, vmid_, sos_timer_period_.data(),
                                          sos_timer_period_.size());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set timeslice:" << status;
    goto unmap_ipa;
  }

  *param.guest_ipa = addr;
  *param.ipa_size = kSharedMemSize;
  share_vmo_ = std::move(shared_vmo);
  sched_handle_ = std::move(sched_handle);
  shared_aee_vmo_ = std::move(shared_aee_vmo);
  guest_aee_ipa_ = aee_addr;
  guest_aee_size_ = aee_size;
  for (int i = 0; i < MAX_NOTIFY_IRQ; i++) {
    notify_irq_[i] = param.notify_irq[i];
  }
  for (int i = 0; i < MAX_GPU_IRQ; i++) {
    gpu_irq_[i] = param.gpu_irq[i];
  }

  gpu_regions_ = param.gpu_regions;
  gpu_reserved_mem_ = param.gpu_reserved_mem;

  // Publish only after the kernel scheduler handle is ready so UOS cannot
  // bind and receive an empty/invalid handle.
  PublishService();
  return ZX_OK;

unmap_ipa:
  zx::unowned_vmar::wrap(guest_->vmar()).unmap(addr, kSharedMemSize);

  return status;
}

zx_status_t GpuSchedulerServiceImpl::destroyGpuSched(uint32_t vmid) {
  return zx_gpu_scheduler_destroy_vm(sched_handle_.get(), vmid);
}

zx_status_t GpuSchedulerServiceImpl::GetGpuebShareMemFromDtb(void* dtb,
                                                             size_t dtb_size) {
  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int offs = fdt_path_offset(dtb, "/reserved-memory");
  if (offs < 0) {
    fdt_pack(dtb);
    return ZX_ERR_NO_RESOURCES;
  }

  offs = fdt_node_offset_by_compatible(dtb, offs, "mediatek,me_GPUEB_SHARED");
  if (offs < 0) {
    gpueb_shmem_paddr_ = 0;
    gpueb_shmem_size_ = 0;
    fdt_pack(dtb);
    return ZX_ERR_NO_RESOURCES;
  }
  uint64_t gpaddr, gsize;
  ret = fdt_getprop_cells_u64(dtb, offs, "reg", 2, &gpaddr, &gsize);
  if (ret < 0) {
    fdt_pack(dtb);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  fdt_pack(dtb);

  gpueb_shmem_paddr_ = gpaddr;
  gpueb_shmem_size_ = gsize;
  return ZX_OK;
};

zx_status_t GpuSchedulerServiceImpl::SetGpuebShareMemToKernel(
    gpu_shed_param& param) {
  zx_status_t status;
  zx::vmo vmo;

  if (sched_handle_.get() == ZX_HANDLE_INVALID)
    return ZX_ERR_BAD_STATE;

  if (gpueb_shmem_size_ == 0 || gpueb_shmem_paddr_ == 0)
    return ZX_ERR_NOT_FOUND;

  status = zx::vmo::create(gpueb_shmem_size_, 0, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create gpu shared memory:" << status;
    return status;
  }

  status =
      zx_vmo_create_physical(param.resource, gpueb_shmem_paddr_,
                             gpueb_shmem_size_, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "swa shared memory failed to create physical VMO:"
                   << status;
    return status;
  }

  status = zx_gpu_scheduler_set_gpueb_shmem_vmo(sched_handle_.get(), vmo.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "swa shared memory failed to set vmo:" << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t GpuSchedulerServiceImpl::addVcpu(zx_handle_t vcpu, bool affinity) {
  return zx_gpu_scheduler_add_vcpu(sched_handle_.get(), vmid_, vcpu, affinity);
}

void GpuSchedulerServiceImpl::GetSchedHandle(GetSchedHandleCallback callback) {
  zx::handle handle;
  if (!IsSchedReady()) {
    FXL_LOG(ERROR) << "Sched handle not ready.";
    callback(zx::handle());
    return;
  }
  auto status = sched_handle_.duplicate(ZX_RIGHT_SAME_RIGHTS, &handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate sched handle.";
    callback(zx::handle());
    return;
  }
  callback(std::move(handle));
}

void GpuSchedulerServiceImpl::GetSchedIrq(GetSchedIrqCallback callback) {
  SchedIrqResult res = {};
  if (!IsSchedReady()) {
    FXL_LOG(ERROR) << "Sched IRQ query before scheduler ready.";
    callback(res);
    return;
  }

  res.grant_irq = notify_irq_[GRANT_IRQ_INX];
  res.stop_irq = notify_irq_[STOP_IRQ_INX];
  res.lost_irq = notify_irq_[LOST_IRQ_INX];
  res.rcvr_irq = notify_irq_[RCVR_IRQ_INX];
  res.job_irq = gpu_irq_[JOB_IRQ_INX];
  res.mmu_irq = gpu_irq_[MMU_IRQ_INX];
  res.gpu_irq = gpu_irq_[GPU_IRQ_INX];
  res.evt_irq = gpu_irq_[EVT_IRQ_INX];
  res.pwr_irq = gpu_irq_[PWR_IRQ_INX];

  callback(res);
}

void GpuSchedulerServiceImpl::GetSchedReg(GetSchedRegCallback callback) {
  SchedRegResult res;
  res.gpu_regions.resize(0);
  if (!IsSchedReady()) {
    FXL_LOG(ERROR) << "Sched reg query before scheduler ready.";
    callback(std::move(res));
    return;
  }

  for (const auto& item : gpu_regions_) {
    res.gpu_regions.push_back(item);
  }

  callback(std::move(res));
}

void GpuSchedulerServiceImpl::GetSchedTime(GetSchedTimeCallback callback) {
  SchedTimeResult res;
  res.timer_period.resize(0);
  if (!IsSchedReady()) {
    FXL_LOG(ERROR) << "Sched time query before scheduler ready.";
    callback(std::move(res));
    return;
  }

  for (const auto& item : uos_timer_period_) {
    res.timer_period.push_back(item);
  }

  callback(std::move(res));
}

void GpuSchedulerServiceImpl::GetSchedRsv(GetSchedRsvCallback callback) {
  SchedRsvResult res = {};
  if (!IsSchedReady()) {
    FXL_LOG(ERROR) << "Sched rsv query before scheduler ready.";
    callback(res);
    return;
  }

  res.gpu_reserved_mem = gpu_reserved_mem_;
  callback(res);
}

void GpuSchedulerServiceImpl::GetSchedAeeInfo(
    GetSchedAeeInfoCallback callback) {
  SchedAeeInfoResult res = {};
  if (!IsSchedReady()) {
    FXL_LOG(ERROR) << "Sched AEE query before scheduler ready.";
    callback(res);
    return;
  }
  res.aee_ipa = guest_aee_ipa_;
  res.aee_size = guest_aee_size_;
  callback(res);
}

GpuSchedulerClient::GpuSchedulerClient(Guest* guest) : guest_(guest) {}

zx_status_t GpuSchedulerClient::prepareGpuSched(zx_handle_t resource) {
  zx::vmo shared_vmo;
  zx_status_t status;
  uint64_t addr;
  SchedIrqResult sched_irq_res;
  SchedRegResult sched_mem_reg;
  SchedTimeResult sched_time_res;
  SchedRsvResult sched_mem_rsv;
  SchedAeeInfoResult sched_aee_info_res;
  std::vector<uint64_t> uos_timer_period;
  bool res;

  res = svc_->GetSchedHandle(&sched_handle_);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to get gpu schedule handle.";
    return ZX_ERR_BAD_HANDLE;
  }
  if (!sched_handle_ || sched_handle_.get() == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Gpu schedule handle is invalid (scheduler not ready).";
    return ZX_ERR_BAD_STATE;
  }

  res = svc_->GetSchedIrq(&sched_irq_res);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to get gpu schedule irq.";
    return ZX_ERR_BAD_HANDLE;
  }

  res = svc_->GetSchedReg(&sched_mem_reg);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to get gpu schedule memory.";
    return ZX_ERR_BAD_HANDLE;
  }

  res = svc_->GetSchedTime(&sched_time_res);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to get gpu schedule time.";
    return ZX_ERR_BAD_HANDLE;
  }

  res = svc_->GetSchedRsv(&sched_mem_rsv);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to get gpu schedule reserved.";
    return ZX_ERR_BAD_HANDLE;
  }

  res = svc_->GetSchedAeeInfo(&sched_aee_info_res);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to get gpu schedule aee info.";
    return ZX_ERR_BAD_HANDLE;
  }
  guest_aee_ipa_ = sched_aee_info_res.aee_ipa;
  guest_aee_size_ = (size_t)sched_aee_info_res.aee_size;
  FXL_LOG(INFO) << "[swa-aee]GpuSchedulerClient get aee info("
                << sched_aee_info_res.aee_ipa << ", "
                << sched_aee_info_res.aee_size << ") done\n";

  uintptr_t swa_addr = 0;
  if (guest_aee_size_ > 0 && 1 == guest_->vmid()) {
    guest_aee_ipa_ = 0;
    guest_aee_size_ = 0;
    FXL_LOG(INFO) << "[swa-aee]GpuSchedulerClient tbox not add arbiter-aee vmo\n";
  }
  if (guest_aee_size_ > 0) {
    zx::vmo swa_vmo;
    status = zx_vmo_create_physical(resource, guest_aee_ipa_, guest_aee_size_,
                                    swa_vmo.reset_and_get_address());
    if (status != ZX_OK) {
      FXL_LOG(ERROR)
          << "[swa-aee]GpuSchedulerClient failed to create physical VMO:"
          << status << "\n";
      return status;
    }
    status = swa_vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
    if (status != ZX_OK) {
      FXL_LOG(ERROR)
          << "[swa-aee]GpuSchedulerClient failed to set cache policy on VMO:"
          << status << "\n";
      return status;
    }
    status = zx_vmar_map(
        guest_->vmar(),
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
            ZX_VM_FLAG_PERM_EXECUTE | ZX_VM_FLAG_SPECIFIC |
            ZX_VM_FLAG_MAP_CONTIGUOUS,
        guest_aee_ipa_, swa_vmo.get(), 0, guest_aee_size_, &swa_addr);

    if (status != ZX_OK) {
      FXL_LOG(ERROR)
          << "[swa-aee]GpuSchedulerClient failed to map guest physical memory:"
          << status << "\n";
      return status;
    }
    FXL_LOG(INFO) << "[swa-aee]GpuSchedulerClient add arbiter-aee vmo("
                  << guest_aee_ipa_ << ", " << guest_aee_size_ << ", "
                  << swa_addr << ") done\n";
  }

  status = zx::vmo::create(kSharedMemSize, 0, &shared_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create shared memory:" << status;
    return status;
  }

  status =
      zx::unowned_vmar::wrap(guest_->vmar())
          .map(kShareMemBase, shared_vmo, 0, kSharedMemSize, kMapFlags, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map shared memory:" << status;
    return status;
  }

  status =
      zx_gpu_scheduler_set_vmo(sched_handle_.get(), vmid_, shared_vmo.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set vmo:" << status;
    goto unmap_ipa;
  }

  uos_timer_period = sched_time_res.timer_period;
  status = zx_gpu_scheduler_set_timeslice(sched_handle_.get(), vmid_,
                                          uos_timer_period.data(),
                                          uos_timer_period.size());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set timeslice:" << status;
    goto unmap_ipa;
  }

  guest_ipa_ = addr;
  ipa_size_ = kSharedMemSize;
  share_vmo_ = std::move(shared_vmo);

  notify_irq_[GRANT_IRQ_INX] = sched_irq_res.grant_irq;
  notify_irq_[STOP_IRQ_INX] = sched_irq_res.stop_irq;
  notify_irq_[LOST_IRQ_INX] = sched_irq_res.lost_irq;
  notify_irq_[RCVR_IRQ_INX] = sched_irq_res.rcvr_irq;
  gpu_irq_[JOB_IRQ_INX] = sched_irq_res.job_irq;
  gpu_irq_[MMU_IRQ_INX] = sched_irq_res.mmu_irq;
  gpu_irq_[GPU_IRQ_INX] = sched_irq_res.gpu_irq;
  gpu_irq_[EVT_IRQ_INX] = sched_irq_res.evt_irq;
  gpu_irq_[PWR_IRQ_INX] = sched_irq_res.pwr_irq;

  gpu_regions_ = sched_mem_reg.gpu_regions;
  gpu_reserved_mem_ = sched_mem_rsv.gpu_reserved_mem;

  return ZX_OK;

unmap_ipa:
  zx::unowned_vmar::wrap(guest_->vmar()).unmap(addr, kSharedMemSize);
  if (0 != swa_addr) {
    zx_vmar_unmap(guest_->vmar(), guest_aee_ipa_, guest_aee_size_);
  }

  return status;
}

zx_status_t GpuSchedulerClient::addVcpu(zx_handle_t vcpu, bool affinity) {
  return zx_gpu_scheduler_add_vcpu(sched_handle_.get(), vmid_, vcpu, affinity);
}

void GpuSchedulerClient::GetGpuSchedInfo(uint64_t& guest_ipa,
                                         uint32_t& ipa_size,
                                         uint32_t notify_irq[],
                                         uint32_t gpu_irq[],
                                         std::vector<uint64_t>& gpu_regions,
                                         bool& gpu_reserved_mem) {
  guest_ipa = guest_ipa_;
  ipa_size = ipa_size_;
  for (int i = 0; i < MAX_GPU_IRQ; i++) {
    gpu_irq[i] = gpu_irq_[i];
  }
  for (int i = 0; i < MAX_NOTIFY_IRQ; i++) {
    notify_irq[i] = notify_irq_[i];
  }

  gpu_regions = gpu_regions_;
  gpu_reserved_mem = gpu_reserved_mem_;
}

void GpuSchedulerClient::GetGpuSchedAeeInfo(uint64_t& guest_aee_ipa,
                                            size_t& guest_aee_size) {
  guest_aee_ipa = guest_aee_ipa_;
  guest_aee_size = guest_aee_size_;
}

}  // namespace machina
