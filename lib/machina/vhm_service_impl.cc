// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2020 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/vhm_service_impl.h"

#ifdef _VHM_USE_CHANNEL_
#include "garnet/lib/machina/vhm_chan_message.h"
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <trace/event.h>

#include <acrn/acrn_common.h>
#include <acrn/atomic.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <nbl_fwk/nmt_tee.h>
#include <trusty_std.h>
#include <uapi/err.h>
#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>

#include "dump.h"
#include "garnet/lib/machina/monitor_virtio.h"
#include "utrace.h"

#define HC_CREATE_VM 0x80000010
#define HC_DESTROY_VM 0x80000011
#define HC_START_VM 0x80000012
#define HC_SET_VM_CONFIG 0x80000017
#define HC_ADD_MMIO_MAPPING 0x80000018
#define HC_INJECT_MSI 0x80000023
#define HC_SET_IOREQ_BUFFER 0x80000030
#define HC_NOTIFY_REQUEST_FINISH 0x80000031
#define HC_CALL 0x80000090
#define HC_MMIO_TRAP_DUMP 0x80000061

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static constexpr zx_signals_t kSignalVhmShutdown = ZX_USER_SIGNAL_0;
static constexpr uint64_t kVmReadyTimeoutSeconds = 5;
static constexpr zx::duration kVmReadyTimeout = zx::sec(kVmReadyTimeoutSeconds);

static zx_status_t get_root_resource(zx::resource* resource) {
  FXL_CHECK(resource != nullptr) << "resource is null";
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

namespace machina {

VhmServiceImpl::VhmServiceImpl(component::ServiceProviderBridge* bridge,
                               component::ApplicationLauncherPtr& launcher,
                               InterruptController* interrupt_controller,
                               uint32_t kick_irq,
                               const PhysMem& phys_mem,
                               uintptr_t phys_mem_base,
                               Guest* guest)
    : launcher_(launcher),
      phys_mem_(phys_mem),
      phys_mem_base_(phys_mem_base),
      interrupt_controller_(interrupt_controller),
      kick_irq_(kick_irq),
      guest_(guest) {
  bridge->AddService<VhmService>(
      [this](fidl::InterfaceRequest<VhmService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

zx_status_t VhmServiceImpl::HandleVhmRequest(zx_vcpu_state_t* state) {
  FXL_CHECK(state != nullptr);
  uint64_t id = state->x[1];
  uint64_t p0 = state->x[2];
  uint64_t p1 = state->x[3];
  uint64_t p2 = state->x[4];
  uint64_t p3 = state->x[5];
  uint64_t p4 = state->x[6];
  uint64_t result;

  switch (id) {
    // Place the most frequently called case at the beginning of the
    // switch statement to improve branch hit rate
    case HC_INJECT_MSI: {
      auto msi = phys_mem_.as<acrn_msi_entry>(to_offset(p1));
#ifdef WITH_CROSS_VM_DUMP
      cvmd_tp_add_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 2, msi->msi_addr);
#endif
      result = inject_msi(p0, p1);
      break;
    }
    case HC_NOTIFY_REQUEST_FINISH:
#ifdef WITH_CROSS_VM_DUMP
      cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 11);
#endif
      result = signal_ioreq_completion(p0, p1);
      break;
    case HC_CREATE_VM:
      result = create_vm(p0);
      break;
    case HC_DESTROY_VM:
      result = destroy_vm(p0);
      FXL_LOG(INFO) << "VM - " << p0 << " Destroyed";
      break;
    case HC_START_VM:
      result = start_vm(p0);
      break;
    case HC_SET_VM_CONFIG:
      result = set_vm_config(p0, p1, p2);
      break;
    case HC_ADD_MMIO_MAPPING:
      result = add_mmio_mapping(p0, p1);
      break;
    case HC_SET_IOREQ_BUFFER:
      result = set_ioreq_buffer(p0, p1);
      break;
#ifdef VHE_MMIO_TRAP_DEBUG
    case HC_MMIO_TRAP_DUMP:
      result = mmio_trap_dump(p0);
      break;
#endif
    case HC_CALL:
      result = vm_call(p0, p1, p2, p3, p4);
      break;
    default:
      FXL_LOG(ERROR) << "Unhandled Vhm command " << std::hex << id;
      result = -1;
  }

  state->x[0] = result;
  return ZX_OK;
}

std::shared_ptr<VmContext> VhmServiceImpl::FindActiveVmContextLocked(
    uint64_t vmid) {
  auto it = vm_map_.find(vmid);
  if (it == vm_map_.end() || it->second.shutting_down) {
    return nullptr;
  }
  return it->second.context;
}

uint64_t VhmServiceImpl::create_vm(uint64_t create_vm_paddr) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);
  auto create_vm = phys_mem_.as<acrn_vm_creation>(to_offset(create_vm_paddr));

  auto it = vm_map_.find(create_vm->vmid);
  if (it != vm_map_.end()) {
    FXL_LOG(ERROR) << "Guest vmid " << create_vm->vmid
                   << " is already existed or shutting down";
    return -1;
  }

  int rc = nbl_act_feature(NMT_ACT_FEAT_MAX_VM);

  if (rc < 0) {
    int max_retries = 2;
    int rc_retry = rc;

    for (int i = 0; i < max_retries; i++) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
        rc_retry = nbl_act_feature(NMT_ACT_FEAT_MAX_VM);

        FXL_LOG(ERROR) << "license max_vm failed, retry " << (i+1)
                       << ", orig rc=" << rc
                       << ", retry rc=" << rc_retry;

        if (rc_retry >= 0) {
            break;
        }
    }

    rc = rc_retry;
  }

  if (rc < 0) {
    FXL_LOG(ERROR) << "Failed to query license [max_vm], rc=" << rc;
    return -1;
  }

  /* vm_map_.size() + 1: number of uos + single sos */
  if (vm_map_.size() + 1 >= rc) {
    FXL_LOG(ERROR) << "Number of VMs reach license [max_vm]: " << rc;
    return -1;
  }

  uint64_t cpu_affinity = create_vm->cpu_affinity;
  uint8_t pcpu_id;
  while (cpu_affinity != 0) {
    pcpu_id = 63 - __builtin_clzll(cpu_affinity);
    if (pcpu_id >= zx_system_get_num_cpus()) {
      FXL_LOG(ERROR) << "You can't bind to a non-exist cpu:" << pcpu_id;
      return -1;
    }
    cpu_affinity &= ~(1 << pcpu_id);
  }

  auto vm = std::make_shared<VmContext>(create_vm->vmid, create_vm->vcpu_num,
                                        create_vm->cpu_affinity, launcher_,
                                        interrupt_controller_, kick_irq_);
  if (!vm) {
    FXL_LOG(ERROR) << "Failed to create VmContext";
    return -1;
  }

  vm->set_uos_exp_irq(uos_exp_irq_);
  FXL_LOG(INFO) << "VM - " << vm->vmid() << " Created";
  uint16_t vmid = vm->vmid();
  vm_map_.emplace(vmid, VmEntry{std::move(vm), false});
  return 0;
}

uint64_t VhmServiceImpl::inject_msi(uint64_t vmid, uint64_t inject_msi_paddr) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);
  auto msi = phys_mem_.as<acrn_msi_entry>(to_offset(inject_msi_paddr));
  auto context = FindActiveVmContextLocked(vmid);
#ifdef WITH_CROSS_VM_DUMP
  cvmd_tp_add_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 3, inject_msi_paddr);
#endif
  if (likely(context)) {
#ifdef WITH_CROSS_VM_DUMP
    cvmd_tp_add(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 4, inject_msi_paddr,
                msi->msi_addr);
#endif
    context->send_interrupt(msi->msi_addr, msi->msi_data);
    return 0;
  } else {
#ifdef WITH_CROSS_VM_DUMP
    cvmd_tp_add_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 5, inject_msi_paddr);
#endif
    FXL_LOG(ERROR) << "vmid " << vmid << " not found";
    return -1;
  }
}

uint64_t VhmServiceImpl::destroy_vm(uint64_t vmid) {
  std::shared_ptr<VmContext> context;

  {
    std::lock_guard<std::mutex> lock(vm_map_lock_);

    auto it = vm_map_.find(vmid);
    if (it == vm_map_.end()) {
      FXL_LOG(ERROR) << "vmid " << vmid << " not found";
      return -1;
    }
    if (it->second.shutting_down) {
      FXL_LOG(ERROR) << "vmid " << vmid << " is already shutting down";
      return -1;
    }

    it->second.shutting_down = true;
    context = it->second.context;
  }

  for (uint32_t vcpu = 0; vcpu < ACRN_IO_REQUEST_MAX; ++vcpu) {
    context->signal_ioreq_termination(vcpu);
  }
  guest_->TeeVmDestroy((int32_t)vmid);
  context->Shutdown();

  machina::GpuSchedulerServiceImpl* gpu_sched_raw = nullptr;
  gpu_sched_raw = guest_->GetGpuSchedService();
  if (gpu_sched_raw) {
    gpu_sched_raw->destroyGpuSched((int32_t)vmid);
  } else {
    FXL_LOG(INFO) << "No active GpuSchedClient to destroy";
  }

  {
    std::lock_guard<std::mutex> lock(vm_map_lock_);
    auto it = vm_map_.find(vmid);
    if (it != vm_map_.end() && it->second.context == context) {
      vm_map_.erase(it);
    }
  }

  return 0;
}

uint64_t VhmServiceImpl::start_vm(uint64_t vmid) {
  std::shared_ptr<VmContext> context;
  zx_status_t status;
  {
    std::lock_guard<std::mutex> lock(vm_map_lock_);

    context = FindActiveVmContextLocked(vmid);
    if (!context) {
      FXL_LOG(ERROR) << "vmid " << vmid << " not found";
      return -1;
    }

    status = guest_->TeeVmCreate((int32_t)vmid);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Tee vm create failed " << status;
      return status;
    }

    status = context->start_vm();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "VM - " << vmid
                     << " launch task post failed, status: " << status;
      return status;
    }
  }

  // Keep the existing bounded wait for UOS VCPU-0 startup readiness.
  status = context->WaitForVmReady(zx::deadline_after(kVmReadyTimeout));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VM - " << vmid << " Start Failed, status: " << status;
    return status;
  }

  FXL_LOG(INFO) << "VM - " << vmid << " Start Success";
  return 0;
}

uint64_t VhmServiceImpl::set_ioreq_buffer(uint64_t vmid,
                                          uint64_t ioreq_buffer) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    auto buf = phys_mem_.as<acrn_ioreq_buffer>(to_offset(ioreq_buffer));
    context->set_ioreq_buffer(buf->ioreq_buf);
    return 0;
  } else {
    FXL_LOG(ERROR) << "vmid " << vmid << " not found";
    return -1;
  }
}

uint64_t VhmServiceImpl::set_vm_config(uint64_t vmid,
                                       uint64_t config_addr,
                                       uint64_t config_size) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    auto config_data = phys_mem_.as<char>(to_offset(config_addr));
    return context->set_vm_config(config_data, config_size);
  } else {
    FXL_LOG(ERROR) << "vmid " << vmid << " not found";
    return -1;
  }
}

uint64_t VhmServiceImpl::add_mmio_mapping(uint64_t vmid,
                                          uint64_t mmio_mapping_paddr) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    auto acrn_mapping =
        phys_mem_.as<acrn_mmio_mapping>(to_offset(mmio_mapping_paddr));

    auto mapping = std::make_unique<acrn_mmio_mapping>();
    *mapping = *acrn_mapping;
    context->add_mmio_mapping(mapping);
    return 0;
  } else {
    FXL_LOG(ERROR) << "vmid " << vmid << " not found";
    return -1;
  }
}

uint64_t VhmServiceImpl::signal_ioreq_completion(uint64_t vmid,
                                                 uint8_t vcpu_id) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (likely(context)) {
    context->signal_ioreq_completion(vcpu_id);
    return 0;
  } else {
    FXL_LOG(ERROR) << "vmid " << vmid << " not found";
    return -1;
  }
}

uint64_t VhmServiceImpl::vm_call(uint64_t vmid,
                                 uint64_t tx_buf_addr,
                                 uint64_t tx_buf_size,
                                 uint64_t rx_buf_addr,
                                 uint64_t rx_buf_size) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    if (!context->is_message_listener_ready()) {
      FXL_LOG(ERROR) << "vmid " << vmid << " message listener not ready";
      return -1;
    }

    std::string tx_buf(phys_mem_.as<const char>(to_offset(tx_buf_addr)),
                       tx_buf_size);
    auto rx_buf = phys_mem_.as<void*>(to_offset(rx_buf_addr));
    if (context->vm_call(tx_buf, rx_buf, &rx_buf_size) != ZX_OK)
      return -1;

    return rx_buf_size;
  } else {
    FXL_LOG(ERROR) << "vmid " << vmid << " not found";
    return -1;
  }
}

void VhmServiceImpl::GetIoRequestVmo(uint16_t vmid,
                                     GetIoRequestVmoCallback callback) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);
  zx::vmo vmo;

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    context->get_ioreq_vmo(&vmo);
    callback(VhmClientStatus::OK, std::move(vmo));
  } else {
    callback(VhmClientStatus::NOT_FOUND, zx::vmo());
  }
}

void VhmServiceImpl::RegisterInterruptListener(
    uint16_t vmid,
    fidl::InterfaceHandle<InterruptListener> handle,
    RegisterInterruptListenerCallback callback) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    context->SetInterruptListener(std::move(handle));
    callback(VhmClientStatus::OK);
  } else {
    callback(VhmClientStatus::NOT_FOUND);
  }
}

void VhmServiceImpl::RegisterMessageListener(
    uint16_t vmid,
    fidl::InterfaceHandle<MessageListener> handle,
    RegisterMessageListenerCallback callback) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    context->SetMessageListener(std::move(handle));
    callback(VhmClientStatus::OK);
  } else {
    callback(VhmClientStatus::NOT_FOUND);
  }
}

void VhmServiceImpl::GetMmioMappings(
    uint16_t vmid,
    fidl::InterfaceHandle<MappingListener> handle,
    GetMmioMappingsCallback callback) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    context->GetMmioMappings(std::move(handle));
    callback(VhmClientStatus::OK);
  } else {
    callback(VhmClientStatus::NOT_FOUND);
  }
}

void VhmServiceImpl::FetchVmConfig(uint16_t vmid,
                                   FetchVmConfigCallback callback) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);
  fsl::SizedVmo vmo;

  auto context = FindActiveVmContextLocked(vmid);
  if (context) {
    context->get_vm_config(&vmo);
    callback(VhmClientStatus::OK, std::move(vmo).ToTransport());
  } else {
    callback(VhmClientStatus::NOT_FOUND, std::move(vmo).ToTransport());
  }
}

void VhmServiceImpl::Kick() {
  FXL_CHECK(interrupt_controller_ != nullptr);
  interrupt_controller_->Interrupt(kick_irq_);
}

void VhmServiceImpl::WaitForIoRequestCompletion(
    uint16_t vmid,
    uint8_t vcpu_id,
    WaitForIoRequestCompletionCallback callback) {
  std::shared_ptr<VmContext> context;
  {
    std::lock_guard<std::mutex> lock(vm_map_lock_);
    context = FindActiveVmContextLocked(vmid);
  }

  if (context) {
    context->Wait(vcpu_id);
  }

  callback();
}

void VhmServiceImpl::SetupChannel(uint16_t vmid,
                                  SetupChannelCallback callback) {
#ifdef _VHM_USE_CHANNEL_
  // Notice: cli & srv is from uos's viewpoint, not sos.
  // channel used to send message from uos to sos.
  zx::channel cli_local, cli_remote;
  // channel used to send message from sos to uos.
  zx::channel srv_local, srv_remote;
  std::lock_guard<std::mutex> lock(vm_map_lock_);

  auto context = FindActiveVmContextLocked(vmid);
  if (!context) {
    callback(VhmClientStatus::NOT_FOUND, std::move(cli_remote),
             std::move(srv_remote));
    return;
  }

  zx_status_t status = zx::channel::create(0u, &cli_local, &cli_remote);
  FXL_CHECK(status == ZX_OK);

  status = zx::channel::create(0u, &srv_local, &srv_remote);
  FXL_CHECK(status == ZX_OK);

  status = context->SetupVhmChannel(std::move(cli_local), std::move(srv_local));
  if (status != ZX_OK) {
    callback(VhmClientStatus::ALREADY_BOUND, zx::channel(), zx::channel());
    return;
  }

  callback(VhmClientStatus::OK, std::move(cli_remote), std::move(srv_remote));
#endif
}

uint64_t VhmServiceImpl::mmio_trap_dump(uint64_t vmid) {
  std::lock_guard<std::mutex> lock(vm_map_lock_);
  auto context = FindActiveVmContextLocked(vmid);
  if (!context) {
    FXL_LOG(ERROR) << "Guest vmid " << vmid << " is not find";
    return -1;
  }
  return 0;
}

VmContext::VmContext(uint16_t vmid,
                     uint8_t num_vcpus,
                     uint64_t cpu_affinity,
                     component::ApplicationLauncherPtr& launcher,
                     InterruptController* controller,
                     uint32_t kick_irq)
    : vmid_(vmid),
      num_vcpus_(num_vcpus),
      cpu_affinity_(cpu_affinity),
      launcher_(launcher),
      interrupt_controller_(controller),
      kick_irq_(kick_irq) {
  ioreq_event_.resize(ACRN_IO_REQUEST_MAX);
  for (uint8_t i = 0; i < ACRN_IO_REQUEST_MAX; i++) {
    FXL_CHECK(zx::event::create(0, &ioreq_event_[i]) == ZX_OK);
  }
  FXL_CHECK(zx::event::create(0, &vm_ready_event_) == ZX_OK);
  FXL_CHECK(loop_.StartThread() == ZX_OK);
}

VmContext::~VmContext() {
  Shutdown();
  if (ioreq_) {
    zx::vmar::root_self().unmap((uintptr_t)ioreq_, PAGE_SIZE);
    ioreq_ = nullptr;
  }
}

void VmContext::Shutdown() {
  if (shutdown_started_.exchange(true)) {
    return;
  }

  for (uint32_t vcpu = 0; vcpu < ACRN_IO_REQUEST_MAX; ++vcpu) {
    signal_ioreq_termination(vcpu);
  }
  vm_ready_event_.signal(0, kSignalVmShutdown);

  loop_.Shutdown();

  if (application_controller_.is_bound()) {
    application_controller_->KillAndWait();
  }

#ifdef _VHM_USE_CHANNEL_
  if (vhm_srv_chan_.is_valid()) {
    vhm_srv_chan_.signal(0, kSignalVhmShutdown);
  }
  vhm_cli_chan_.reset();
  if (vhm_thread_started_) {
    int result = 0;
    const zx_time_t join_start = zx_clock_get(ZX_CLOCK_MONOTONIC);
    const int ret = thrd_join(vhm_thread_, &result);
    const zx_duration_t join_duration =
        zx_clock_get(ZX_CLOCK_MONOTONIC) - join_start;
    FXL_LOG(INFO) << "vhm-sos-thread join waited " << join_duration << " ns ("
                  << join_duration / ZX_MSEC(1) << " ms), ret=" << ret
                  << ", result=" << result;
    if (ret != thrd_success) {
      FXL_LOG(ERROR) << "vhm-sos-thread join failed";
    }
    vhm_thread_started_ = false;
  }
  vhm_srv_chan_.reset();
#endif
}

void VmContext::Wait(uint8_t vcpu_id) {
  zx_signals_t signals = kSignalIoreqCompletion | kSignalIoreqTermination;
  FXL_DCHECK(ioreq_event_[vcpu_id].is_valid());
  ioreq_event_[vcpu_id].wait_one(signals, zx::time::infinite(), nullptr);
  // TODO: Return the proper status to client for graceful shutdown.
  // For now, keep termination event toggled so clients won't block on
  // wait during termination process.
  ioreq_event_[vcpu_id].signal(kSignalIoreqCompletion, 0);
}

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

void VmContext::set_ioreq_buffer(uint64_t ioreq_buffer) {
  ioreq_buffer_ = ioreq_buffer;

  zx::vmo vmo;
  get_ioreq_vmo(&vmo);

  zx_status_t status = zx::vmar::root_self().map(
      0, vmo, 0, PAGE_SIZE, kMapFlags, (uintptr_t*)&ioreq_);
  FXL_CHECK(status == ZX_OK);

  for (uint8_t i = 0; i < ACRN_IO_REQUEST_MAX; i++) {
    acrn_io_request* req = ioreq_ + i;
    ATOMIC_STORE(&req->processed, ACRN_IOREQ_STATE_FREE);
  }
}

void VmContext::get_vm_config(fsl::SizedVmo* vmo) {
  FXL_CHECK(vmo != nullptr);
  FXL_CHECK(fsl::VmoFromString(vm_config_, vmo));
}

zx_status_t VmContext::set_vm_config(const char* config_data,
                                     uint64_t config_size) {
  FXL_CHECK(config_data != nullptr);
  vm_config_.resize(config_size);
  memcpy(&vm_config_[0], config_data, config_size);

  return ZX_OK;
}

void VmContext::send_interrupt(uint64_t msi_addr, uint64_t msi_data) {
#ifndef _VHM_USE_CHANNEL_
  if (!interrupt_listener_->OnInterrupt(irq)) {
    FXL_LOG(WARNING) << "failed to signal interrupt, id=" << irq;
  }
#else
  if (shutdown_started_.load(std::memory_order_acquire)) {
    return;
  }

  struct vhm_chan_request request;
  request.cmd = VHM_INTR_INJECT;
  request.param.intr_inject.dev_id = msi_addr;
  request.param.intr_inject.evt_id = msi_data;
  utrace(TAG_COMM_ENTER, 1, 4, 1, msi_addr);
  zx_status_t status =
      vhm_cli_chan_.write(0, &request, sizeof(request), NULL, 0);
  if (status != ZX_OK && !shutdown_started_.load(std::memory_order_acquire)) {
    FXL_LOG(ERROR) << "failed to send interrupt to vm:" << vmid_
                   << ", status:" << status;
  }
  utrace(TAG_COMM_EXIT, 1, 4, 1, msi_addr);
#endif
}

zx_status_t VmContext::vm_call(std::string& req,
                               void* resp,
                               size_t* resp_size) {
  FXL_CHECK(resp != nullptr);
  FXL_CHECK(resp_size != nullptr);
  fsl::SizedVmo req_vmo, resp_vmo;
  mem::Buffer resp_rawbuf;

  FXL_CHECK(fsl::VmoFromString(req, &req_vmo));
  FXL_CHECK(req_vmo != 0);

  if (!message_listener_->OnNewCall(std::move(req_vmo).ToTransport(),
                                    &resp_rawbuf)) {
    FXL_LOG(WARNING) << "failed to send message";
    return ZX_ERR_IO;
  }
  FXL_CHECK(fsl::SizedVmo::FromTransport(std::move(resp_rawbuf), &resp_vmo));
  FXL_CHECK(resp_vmo != 0);
  FXL_CHECK(resp_vmo.size() <= *resp_size);
  resp_vmo.vmo().read(resp, /*offset=*/0, resp_vmo.size());
  *resp_size = resp_vmo.size();
  return ZX_OK;
}

zx::vmo VmContext::get_phys_vmo(uint64_t paddr, size_t size) {
  zx::resource rsc;
  zx_status_t status = get_root_resource(&rsc);
  FXL_CHECK(status == ZX_OK);

  zx::vmo vmo;
  status = zx_vmo_create_physical(rsc.get(), paddr, size,
                                  vmo.reset_and_get_address());
  FXL_CHECK(status == ZX_OK);

  status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
  FXL_CHECK(status == ZX_OK);

  return vmo;
}

void VmContext::get_ioreq_vmo(zx::vmo* vmo) {
  FXL_CHECK(vmo != nullptr);
  FXL_CHECK(ioreq_buffer_ != 0);
  *vmo = get_phys_vmo(ioreq_buffer_, PAGE_SIZE);
}

void VmContext::signal_ioreq_completion(uint8_t vcpu_id) {
  acrn_io_request* req = ioreq_ + vcpu_id;
  FXL_CHECK(ATOMIC_LOAD(&req->processed) == ACRN_IOREQ_STATE_COMPLETE);
  ATOMIC_STORE(&req->processed, ACRN_IOREQ_STATE_FREE);

#ifndef _VHM_USE_CHANNEL_
  ioreq_event_[vcpu_id].signal(0, kSignalIoreqCompletion);
#else
  if (is_sideband_chan(vcpu_id)) {
    if (shutdown_started_.load(std::memory_order_acquire)) {
      return;
    }

    uint8_t side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);
    utrace(TAG_COMM_ENTER, 1, 6, 3, side_chan, 2, vcpu_id);
    struct vhm_chan_request request;
    request.cmd = VHM_IO_REQUEST_ASYNC_RESPONSE;
    request.param.io_req.vcpu_id = vcpu_id;
    zx_status_t status =
        vhm_cli_chan_.write(0, &request, sizeof(request), NULL, 0);
    if (status != ZX_OK && !shutdown_started_.load(std::memory_order_acquire)) {
      FXL_LOG(ERROR) << "failed to send async ioreq response to vm:" << vmid_
                     << ", status:" << status;
    }
    utrace(TAG_COMM_EXIT, 1, 6, 3, side_chan, 2, vcpu_id);
#ifdef WITH_CROSS_VM_DUMP
    cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 12);
#endif
  } else {
    utrace(TAG_COMM_ENTER, 1, 6, 2, vcpu_id);
    ioreq_event_[vcpu_id].signal(0, kSignalIoreqCompletion);
    utrace(TAG_COMM_EXIT, 1, 6, 2, vcpu_id);
  }
#endif
}

#ifdef VHE_MMIO_TRAP_DEBUG
void VmContext::increase_trap_cnt(uintptr_t addr) {
  auto monitor_virtio = machina::Monitor_virtio::GetInstance();
  FXL_CHECK(monitor_virtio != nullptr);

  struct virtio_monitor* virtio_monitor_range;
  virtio_monitor_range = monitor_virtio->FindAddrRange(addr, vmid_);
  if (virtio_monitor_range) {
    uint64_t tk;
    virtio_monitor_range->cur_stage = STAGE_SOS;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tk));
    virtio_monitor_range->time = tk;
  }
}
#endif

void VmContext::signal_ioreq_termination(uint8_t vcpu_id) {
  ioreq_event_[vcpu_id].signal(0, kSignalIoreqTermination);
}

void VmContext::SetInterruptListener(
    fidl::InterfaceHandle<InterruptListener> handle) {
  interrupt_listener_.Bind(std::move(handle));
}

void VmContext::SetMessageListener(
    fidl::InterfaceHandle<MessageListener> handle) {
  message_listener_.Bind(std::move(handle));

  // UOS VCPU-0 is started sucessfully if RegisterMessageListener is called.
  // We can notify the 'start_vm' VhmRequest to reply success to the NBL_VMM.
  vm_ready_event_.signal(0, kSignalVmReady);
}

bool VmContext::is_message_listener_ready() {
  return message_listener_.is_bound();
}

void VmContext::GetMmioMappings(fidl::InterfaceHandle<MappingListener> handle) {
  MappingListenerSyncPtr listener;
  listener.Bind(std::move(handle));

  for (auto it = mmio_mappings_.begin(); it != mmio_mappings_.end(); it++) {
    auto success =
        listener->OnNewMapping((*it)->base, (*it)->size, (*it)->trap_type);
    FXL_CHECK(success);
  }
}

zx_status_t VmContext::start_vm() {
  return async::PostTask(loop_.async(), [this] {
    if (shutdown_started_.load(std::memory_order_acquire)) {
      return;
    }
    component::ApplicationLaunchInfo launch_info;
    launch_info.url = "file://uos";
    launch_info.arguments.push_back(std::to_string(vmid_));
    launcher_->CreateApplication(std::move(launch_info),
                                 application_controller_.NewRequest());
  });
}

zx_status_t VmContext::WaitForVmReady(zx::time deadline) {
  zx_signals_t pending = 0;
  zx_status_t status = vm_ready_event_.wait_one(
      kSignalVmReady | kSignalVmShutdown, deadline, &pending);
  if (status != ZX_OK) {
    return status;
  }
  if (pending & kSignalVmShutdown) {
    return ZX_ERR_CANCELED;
  }
  return ZX_OK;
}

void VmContext::add_mmio_mapping(std::unique_ptr<acrn_mmio_mapping>& mapping) {
  mmio_mappings_.push_back(std::move(mapping));
}

#ifdef _VHM_USE_CHANNEL_
void VmContext::HandleVhmChannelMsgLoop() {
  zx_signals_t signals =
      ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | kSignalVhmShutdown;
  zx_signals_t pending = 0;
  zx_status_t status;

  zx_thread_set_priority(kIRQPriority);
  uint32_t cpu_mask = kNormalCpuAffinity;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                (void*)&cpu_mask);

  while (!shutdown_started_.load(std::memory_order_acquire)) {
    status = vhm_srv_chan_.wait_one(signals, zx::time::infinite(), &pending);
    if (status != ZX_OK) {
      if (!shutdown_started_.load(std::memory_order_acquire)) {
        FXL_LOG(ERROR) << "vhm server channel wait failed, status:" << status;
      }
      break;
    }
    if (pending & kSignalVhmShutdown) {
      break;
    }

    if (pending & ZX_CHANNEL_READABLE) {
      struct vhm_chan_request request;
      struct vhm_chan_response response;
      uint32_t actual_bytes = 0;
      uint32_t msg_size = sizeof(request);
      int vcpu_id;
#ifdef VHE_MMIO_TRAP_DEBUG
      uintptr_t addr;
#endif
      status = vhm_srv_chan_.read(0, &request, msg_size, &actual_bytes, nullptr,
                                  0, nullptr);
      if (unlikely(status != ZX_OK)) {
        FXL_LOG(ERROR) << "Failed to read message from vm: " << vmid_
                       << ", status:" << status;
        break;
      }
      if (unlikely(actual_bytes != msg_size)) {
        FXL_LOG(ERROR) << "Wrong message size, expect:[" << msg_size
                       << "], actual:[" << actual_bytes << "]";
        continue;
      }
      switch (request.cmd) {
        case VHM_IO_REQUEST_ASYNC: {
#ifdef WITH_CROSS_VM_DUMP
          cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 4);
#endif
          vcpu_id = request.param.io_req.vcpu_id;
#ifdef VHE_MMIO_TRAP_DEBUG
          addr = request.param.io_req.addr;
          increase_trap_cnt(addr);
#endif
          FXL_CHECK(is_sideband_chan(vcpu_id)) << "vcpu id:" << vcpu_id;
          FXL_CHECK(interrupt_controller_ != nullptr);
          uint8_t side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);
          utrace(TAG_COMM_ENTER, 1, 3, 3, side_chan);
#ifdef _DEDICATED_VCPU_FOR_IOREQ_HANDLER_
          guest_->NotifyEvent();
#else
          interrupt_controller_->Interrupt(kick_irq_);
#endif
          utrace(TAG_COMM_EXIT, 1, 3, 3, side_chan);
#ifdef WITH_CROSS_VM_DUMP
          cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 5);
#endif
          break;
        }
        case VHM_IO_REQUEST: {
          vcpu_id = request.param.io_req.vcpu_id;
#ifdef VHE_MMIO_TRAP_DEBUG
          addr = request.param.io_req.addr;
          increase_trap_cnt(addr);
#endif
          utrace(TAG_COMM_ENTER, 1, 1, 2, vcpu_id);
          FXL_CHECK(vcpu_id >= 0 && vcpu_id < SIDEBAND_CHANNEL_MIN_VCPU);
          FXL_CHECK(interrupt_controller_ != nullptr);
          interrupt_controller_->Interrupt(kick_irq_);
          Wait(vcpu_id);
          response.txid = request.txid;
          response.cmd = request.cmd;
          status = vhm_srv_chan_.write(0, &response, sizeof(response), NULL, 0);
          if (status != ZX_OK &&
              !shutdown_started_.load(std::memory_order_acquire)) {
            FXL_LOG(ERROR) << "Failed to write vhm response to vm: " << vmid_
                           << ", status:" << status;
          }
          utrace(TAG_COMM_EXIT, 1, 1, 2, vcpu_id);
          break;
        }
        case VHM_SOS_INTR_INJECT: {
          interrupt_controller_->Interrupt(uos_exp_irq_);
          break;
        }
        default:
          break;
      }
    }
    if (pending & ZX_CHANNEL_PEER_CLOSED) {
      break;
    }
  }
}

static int vhm_thread_loop(void* ctx) {
  FXL_CHECK(ctx != nullptr);
  VmContext* context = (VmContext*)ctx;
  context->HandleVhmChannelMsgLoop();
  return 0;
}

zx_status_t VmContext::SetupVhmChannel(zx::channel&& srv, zx::channel&& cli) {
  int ret;

  if (vhm_srv_chan_.is_valid()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  vhm_srv_chan_ = std::move(srv);
  vhm_cli_chan_ = std::move(cli);

  ret = thrd_create_with_name(&vhm_thread_, vhm_thread_loop, this,
                              "vhm-sos-thread");
  FXL_CHECK(ret == thrd_success);
  vhm_thread_started_ = true;

  return ZX_OK;
}
#endif

}  // namespace machina
