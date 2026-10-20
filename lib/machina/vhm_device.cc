// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/vhm_device.h"

#ifdef _VHM_USE_CHANNEL_
#include "garnet/lib/machina/vhm_chan_message.h"
#endif

#include <acrn/acrn_common.h>
#include <acrn/atomic.h>
#include <fuchsia/cpp/machina.h>
#include <lib/app/cpp/application_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/zx/port.h>
#include <lib/zx/vmar.h>
#include <trusty_std.h>
#include <uapi/err.h>
#include <zircon/device/sysinfo.h>

#include <trace/event.h>
#include "utrace.h"

#include "dump.h"
#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/monitor_virtio.h"
#include "garnet/lib/machina/vcpu.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

namespace machina {

thread_local size_t ThreadChannel = NUM_VHM_SIDEBAND_CHANNELS;

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
static constexpr zx_signals_t kSignalVhmShutdown = ZX_USER_SIGNAL_0;
static constexpr uint64_t kExceptionPortKey = 0;
static constexpr uint64_t kExceptionPortShutdownKey = 1;
static constexpr uint32_t kSinglePortPacket = 1;
static constexpr zx::duration kVmDumpCompletionTimeout =
    zx::sec(CVMD_EXCEPTION_DUMP_COMPLETION_TIMEOUT_SEC);

enum ExceptionDumpWaitItem {
  kExceptionDumpWaitItemDumpEvent,
  kExceptionDumpWaitItemShutdown,
  kExceptionDumpWaitItemCount,
};

VhmDevice::VhmDevice(uint16_t vmid) : vmid_(vmid), binding_(this) {}

VhmDevice::~VhmDevice() {
  Shutdown();
}

#ifdef _VHM_USE_CHANNEL_

static int vhm_thread_loop(void* param) {
  VhmDevice* device = (VhmDevice*)param;
  device->HandleVhmChannelMsgLoop();
  return 0;
}

static int uos_exception_dump(void* param) {
  VhmDevice* device = (VhmDevice*)param;
  return device->HandleExceptionDumpLoop();
}

int VhmDevice::HandleExceptionDumpLoop() {
  FXL_LOG(ERROR) << "uos_exception_dump enter";

  if (!exception_port_.is_valid()) {
    FXL_LOG(ERROR) << "exception port is not valid";
    return -1;
  }

  // get process handle
  zx_handle_t handle = zx_process_self();

  uint32_t options = 0;
  auto status = zx_task_bind_exception_port(
      handle, exception_port_.get(), kExceptionPortKey, options);
  if (status < 0) {
    FXL_LOG(ERROR) << "unable to bind subject exception port, status: "
                   << status;
    return -1;
  }
  exception_port_bound_.store(true, std::memory_order_release);

  while (!shutdown_started_.load(std::memory_order_acquire)) {
    zx_port_packet_t packet;
    FXL_LOG(ERROR) << "uos_exception_dump zx_port_wait";
    status = exception_port_.wait(zx::time::infinite(), &packet,
                                  kSinglePortPacket);
    if (status < 0) {
      if (shutdown_started_.load(std::memory_order_acquire)) {
        break;
      }
      FXL_LOG(ERROR) << "zx_port_wait failed, status: " << status;
      return -1;
    }

    if (packet.key == kExceptionPortShutdownKey &&
        packet.type == ZX_PKT_TYPE_USER) {
      break;
    }
    if (packet.key != kExceptionPortKey) {
      continue;
    }
    if (!ZX_PKT_IS_EXCEPTION(packet.type)) {
      continue;
    }
    FXL_LOG(ERROR) << "uos_exception_dump  get an exception";

    zx_koid_t packet_tid = packet.exception.tid;
    zx_handle_t thread;
    status =
        zx_object_get_child(handle, packet_tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0) {
      FXL_LOG(ERROR) << "zx_object_get_child failed, status: " << status;
      return -1;
    }
    zx::thread exception_thread(thread);

    Guest* guest = GetGuest();
    if (guest == nullptr ||
        shutdown_started_.load(std::memory_order_acquire)) {
      status = zx_task_resume(exception_thread.get(),
                              ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT);
      if (status < 0) {
        FXL_LOG(ERROR) << "zx_task_resume failed during shutdown, status: "
                       << status;
        return -1;
      }
      continue;
    }
    guest->SetUosException(true);

    cmvd_cmd_t cmd = {};
    cmd.cmd = CVMD_CMD_EXCEPTION_DUMP;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_CVMD_CMD, (void*)&cmd);

    FXL_LOG(ERROR) << "uos_exception_dump, wait dump signal";
    // wait vm dump complete
    zx::event* dump_evnet = guest->vm_dmp_event();
    zx_wait_item_t wait_items[kExceptionDumpWaitItemCount] = {};
    wait_items[kExceptionDumpWaitItemDumpEvent].handle = dump_evnet->get();
    wait_items[kExceptionDumpWaitItemDumpEvent].waitfor =
        VhmDevice::kSignalVmDump;
    wait_items[kExceptionDumpWaitItemShutdown].handle =
        vhm_shutdown_event_.get();
    wait_items[kExceptionDumpWaitItemShutdown].waitfor = kSignalVhmShutdown;
    status = zx_object_wait_many(
        wait_items, kExceptionDumpWaitItemCount,
        zx::deadline_after(kVmDumpCompletionTimeout).get());
    guest->SetUosException(false);
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT &&
        !shutdown_started_.load(std::memory_order_acquire)) {
      FXL_LOG(ERROR) << "uos_exception_dump wait failed, status: " << status;
    }

    FXL_LOG(ERROR) << "uos_exception_dump, wait dump signal done";
    uint32_t resume_flags = ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT;
    status = zx_task_resume(exception_thread.get(), resume_flags);
    if (status < 0) {
      FXL_LOG(ERROR) << "zx_task_resume failed, status: " << status;
      return -1;
    }
    break;
  }
  if (exception_port_bound_.exchange(false)) {
    status = zx_task_bind_exception_port(
        handle, ZX_HANDLE_INVALID, kExceptionPortKey,
        ZX_EXCEPTION_PORT_UNBIND_QUIETLY);
    if (status < 0) {
      FXL_LOG(ERROR) << "unable to unbind subject exception port, status: "
                     << status;
    }
  }
  return 0;
}

#endif

zx_status_t VhmDevice::Init(Guest* guest,
                            InterruptController* interrupt_controller,
                            GicIts* gic_its) {
  InitInternal();

  guest_ = guest;
  interrupt_controller_ = interrupt_controller;
  gic_its_ = gic_its;

  async::Loop loop;
  fidl::Binding<MappingListener> listener(this);
  machina::VhmClientStatus err;
  vhm_client_->GetMmioMappings(vmid_, listener.NewBinding(loop.async()), &err);
  FXL_CHECK(err == machina::VhmClientStatus::OK);

#ifdef _VHM_USE_CHANNEL_
  next_txid_ = std::make_unique<std::atomic<zx_txid_t>>();
  vhm_client_->SetupChannel(vmid_, &err, &vhm_cli_chan_, &vhm_srv_chan_);
  FXL_CHECK(err == machina::VhmClientStatus::OK);

  std::unique_ptr<uint8_t[]> state(new uint8_t[NUM_VHM_SIDEBAND_CHANNELS]());
  side_chan_state_ = std::move(state);

  std::unique_ptr<std::mutex[]> mutex(
      new std::mutex[NUM_VHM_SIDEBAND_CHANNELS]);
  side_chan_mutex_ = std::move(mutex);

  std::unique_ptr<std::condition_variable[]> cv(
      new std::condition_variable[NUM_VHM_SIDEBAND_CHANNELS]);
  side_chan_cv_ = std::move(cv);

  zx_status_t status = zx::event::create(0, &vhm_shutdown_event_);
  FXL_CHECK(status == ZX_OK);
  status = zx::port::create(0, &exception_port_);
  FXL_CHECK(status == ZX_OK);

  int ret;
  ret = thrd_create_with_name(&vhm_thread_, vhm_thread_loop, this,
                              "vhm-uos-thread");
  FXL_CHECK(ret == thrd_success);
  vhm_thread_started_ = true;

  ret = thrd_create_with_name(&edump_thread_, uos_exception_dump, this,
                              "edump-thread");
  FXL_CHECK(ret == thrd_success);
  edump_thread_started_ = true;
#endif

  loop.RunUntilIdle();
  return ZX_OK;
}

void VhmDevice::InitInternal() {
  machina::VhmClientStatus err;
  vhm_client_->GetIoRequestVmo(vmid_, &err, &ioreq_vmo_);
  FXL_CHECK(err == machina::VhmClientStatus::OK);

  zx_status_t status = zx::vmar::root_self().map(
      0, ioreq_vmo_, 0, PAGE_SIZE, kMapFlags, (uintptr_t*)&ioreq_buf_);
  FXL_CHECK(status == ZX_OK);

  FXL_CHECK(loop_.StartThread() == ZX_OK);
  loop_started_ = true;

  vhm_client_->RegisterInterruptListener(
      vmid_, binding_.NewBinding(loop_.async()), &err);
  FXL_CHECK(err == machina::VhmClientStatus::OK);
}

void VhmDevice::Shutdown() {
  if (shutdown_started_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

#ifdef _VHM_USE_CHANNEL_
  NotifySidebandWaiters();
  if (vhm_shutdown_event_.is_valid()) {
    vhm_shutdown_event_.signal(0, kSignalVhmShutdown);
  }
  if (vhm_srv_chan_.is_valid()) {
    vhm_srv_chan_.signal(0, kSignalVhmShutdown);
  }
  QueueExceptionPortShutdown();

  vhm_cli_chan_.reset();
  JoinThread(vhm_thread_, &vhm_thread_started_, "vhm-uos-thread");
  vhm_srv_chan_.reset();

  JoinThread(edump_thread_, &edump_thread_started_, "edump-thread");
  vhm_shutdown_event_.reset();
  exception_port_.reset();
#endif

  if (binding_.is_bound()) {
    binding_.Unbind();
  }
  if (vhm_client_.is_bound()) {
    vhm_client_.Bind(zx::channel());
  }
  if (loop_started_) {
    loop_.Shutdown();
    loop_started_ = false;
  }

  if (ioreq_buf_ != nullptr) {
    zx::vmar::root_self().unmap((uintptr_t)ioreq_buf_, PAGE_SIZE);
    ioreq_buf_ = nullptr;
  }
}

#ifdef _VHM_USE_CHANNEL_

void VhmDevice::NotifySidebandWaiters() {
  if (!side_chan_state_ || !side_chan_mutex_ || !side_chan_cv_) {
    return;
  }
  for (size_t chn = 0; chn < NUM_VHM_SIDEBAND_CHANNELS; ++chn) {
    {
      std::lock_guard<std::mutex> lock(side_chan_mutex_[chn]);
      side_chan_state_[chn] = 0;
    }
    side_chan_cv_[chn].notify_all();
  }
}

void VhmDevice::JoinThread(thrd_t thread,
                           bool* started,
                           const char* thread_name) {
  if (!started || !*started) {
    return;
  }

  int result = 0;
  const zx_time_t join_start = zx_clock_get(ZX_CLOCK_MONOTONIC);
  const int ret = thrd_join(thread, &result);
  const zx_duration_t join_duration =
      zx_clock_get(ZX_CLOCK_MONOTONIC) - join_start;
  FXL_LOG(INFO) << thread_name << " join waited " << join_duration << " ns ("
                << join_duration / ZX_MSEC(1) << " ms), ret=" << ret
                << ", result=" << result;
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << thread_name << " join failed";
  }
  *started = false;
}

zx_status_t VhmDevice::QueueExceptionPortShutdown() {
  if (!exception_port_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  zx_port_packet_t packet = {};
  packet.key = kExceptionPortShutdownKey;
  packet.type = ZX_PKT_TYPE_USER;
  return exception_port_.queue(&packet, 0);
}

#endif

void VhmDevice::RegisterMessageListener(
    fidl::InterfaceHandle<MessageListener> listener) {
  machina::VhmClientStatus err;
  vhm_client_->RegisterMessageListener(vmid_, std::move(listener), &err);
  FXL_CHECK(err == machina::VhmClientStatus::OK);
  FXL_LOG(INFO) << "VM - " << vmid_ << " RegisterMessageListener Success";
}

void VhmDevice::FetchVmConfig() {
  machina::VhmClientStatus err;
  mem::Buffer buffer;
  vhm_client_->FetchVmConfig(vmid_, &err, &buffer);
  FXL_CHECK(err == machina::VhmClientStatus::OK);

  fsl::SizedVmo result;
  FXL_CHECK(fsl::SizedVmo::FromTransport(std::move(buffer), &result));
  FXL_CHECK(fsl::StringFromVmo(result, &config_));
}

void VhmDevice::make_io_request(acrn_io_request* req,
                                uint8_t direction,
                                uint8_t size,
                                uintptr_t addr) const {
  req->type = ACRN_IOREQ_TYPE_MMIO;
  req->reqs.mmio_request.direction = direction;
  req->reqs.mmio_request.address = addr;
  req->reqs.mmio_request.size = size;
  ATOMIC_STORE(&req->processed, ACRN_IOREQ_STATE_PENDING);

#ifndef _VHM_USE_CHANNEL_
  vhm_client_->Kick();
#endif
}

void VhmDevice::OnInterrupt(uint32_t irq) {
  FXL_CHECK(interrupt_controller_ != nullptr);
  zx_status_t status = interrupt_controller_->Interrupt(irq);
  FXL_CHECK(status == ZX_OK);
}

void VhmDevice::OnNewMapping(uint64_t base, uint64_t size, uint8_t type) {
  auto trap_type = static_cast<TrapType>(type);
  zx_status_t status = guest_->CreateMapping(trap_type, base, size, base, this);
  FXL_CHECK(status == ZX_OK) << "unable to create mapping";
}

zx_status_t VhmDevice::Read(uintptr_t addr, IoValue* value) const {
  auto vcpu = Vcpu::GetCurrent();
  FXL_CHECK(vcpu != nullptr) << "should be invoked from vcpu thread";
  return ReadInternal(vcpu->id(), addr, value);
}

// static
size_t VhmDevice::GetChannel() {
  return ThreadChannel;
}

zx_status_t VhmDevice::Write(uintptr_t addr, const IoValue& value) {
#ifdef VHE_MMIO_TRAP_DEBUG
  auto monitor_virtio = machina::Monitor_virtio::GetInstance();
  FXL_CHECK(monitor_virtio != nullptr);

  struct virtio_monitor* virtio_monitor_range;
  virtio_monitor_range = monitor_virtio->FindAddrRange(addr, vmid_);
  if (virtio_monitor_range) {
    uint64_t tk;
    virtio_monitor_range->cur_stage = STAGE_UOS;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(tk));
    virtio_monitor_range->time = tk;
  }
#endif
  auto vcpu = Vcpu::GetCurrent();
  if (vcpu != nullptr) {
    return WriteInternal(vcpu->id(), addr, value);
  } else {
    // BELL trap handler will not be executed on VCPU thread. Thus, we use
    // sideband channel to transfer vhm request
    size_t chn = GetChannel();
    if (unlikely(chn >= NUM_VHM_SIDEBAND_CHANNELS)) {
      auto name = fsl::GetCurrentThreadName();
      FXL_CHECK(sscanf(name.c_str(), "trapbell-handler-%zu", &chn) == 1);
      FXL_CHECK(chn < NUM_VHM_SIDEBAND_CHANNELS) << "channel:" << chn;
      ThreadChannel = chn;
    }
    auto vcpu_id = SIDEBAND_CHANNEL_VCPU_ID(chn);
#ifndef _VHM_USE_CHANNEL_
    return WriteInternal(vcpu_id, addr, value);
#else
    return WriteInternalAsync(vcpu_id, addr, value);
#endif
  }
}

#ifdef _VHM_USE_CHANNEL_

zx_txid_t VhmDevice::GetNextTxid() const {
  zx_txid_t txid = 0;
  while (!txid) {
    if (shutdown_started_.load(std::memory_order_acquire)) {
      return 0;
    }
    txid = next_txid_->fetch_add(1, std::memory_order_relaxed);
  }
  return txid;
}

zx_status_t VhmDevice::KickAndWaitIoRequest(uint8_t vcpu_id,
                                            uintptr_t addr) const {
  if (shutdown_started_.load(std::memory_order_acquire)) {
    return ZX_ERR_CANCELED;
  }

  struct vhm_chan_request request;
  struct vhm_chan_response response;
  zx_status_t status;
  utrace(TAG_COMM_ENTER, 1, 1, 2, vcpu_id);

  request.txid = GetNextTxid();
  if (!request.txid) {
    return ZX_ERR_CANCELED;
  }
  request.cmd = VHM_IO_REQUEST;
  request.param.io_req.vcpu_id = vcpu_id;
#ifdef VHE_MMIO_TRAP_DEBUG
  request.param.io_req.addr = addr;
#endif
  zx_channel_call_args_t args;
  args.wr_bytes = &request;
  args.wr_num_bytes = sizeof(request);
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = &response;
  args.rd_num_bytes = sizeof(response);
  args.rd_num_handles = 0;
  args.rd_handles = NULL;

  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status = ZX_OK;
  status = vhm_cli_chan_.call(0, zx::time::infinite(), &args, &bytes_read,
                              &handles_read, &read_status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to kick vm:" << vmid_;
    return status;
  }
  utrace(TAG_COMM_EXIT, 1, 1, 2, vcpu_id);
  return read_status;
}

zx_status_t VhmDevice::KickIoRequestAsync(uint8_t vcpu_id,
                                          uintptr_t addr,
                                          acrn_io_request* req,
                                          const IoValue& value) const {
  if (shutdown_started_.load(std::memory_order_acquire)) {
    return ZX_ERR_CANCELED;
  }

  struct vhm_chan_request request;
  zx_status_t status;
  uint8_t side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);
  std::mutex& mutex = side_chan_mutex_[side_chan];
  std::condition_variable& cv = side_chan_cv_[side_chan];
  {
#ifdef WITH_CROSS_VM_DUMP
    cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 0);
#endif
    utrace(TAG_COMM_ENTER, 1, 2, 3, side_chan);
    std::unique_lock<std::mutex> lock(mutex);
#ifdef WITH_CROSS_VM_DUMP
    cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 1);
#endif
    if (shutdown_started_.load(std::memory_order_acquire)) {
      return ZX_ERR_CANCELED;
    }
    if (side_chan_state_[side_chan] != SIDE_CHAN_BUSY) {
      side_chan_state_[side_chan] = SIDE_CHAN_BUSY;
    } else {
      cv.wait(lock, [this, side_chan] {
        return shutdown_started_.load(std::memory_order_acquire) ||
               side_chan_state_[side_chan] != SIDE_CHAN_BUSY;
      });
      if (shutdown_started_.load(std::memory_order_acquire)) {
        return ZX_ERR_CANCELED;
      }
      side_chan_state_[side_chan] = SIDE_CHAN_BUSY;
    }
    utrace(TAG_COMM_EXIT, 1, 2, 3, side_chan);
  }
#ifdef WITH_CROSS_VM_DUMP
  cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 2);
#endif
  memcpy(&req->reqs.mmio_request.value, value.data, value.access_size);
  make_io_request(req, ACRN_IOREQ_DIR_WRITE, value.access_size, addr);

  request.cmd = VHM_IO_REQUEST_ASYNC;
  request.param.io_req.vcpu_id = vcpu_id;
#ifdef VHE_MMIO_TRAP_DEBUG
  request.param.io_req.addr = addr;
#endif
  status = vhm_cli_chan_.write(0, &request, sizeof(request), NULL, 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to kick vm:" << vmid_;
    {
      std::lock_guard<std::mutex> lock(mutex);
      side_chan_state_[side_chan] = 0;
    }
    cv.notify_all();
    return status;
  }
#ifdef WITH_CROSS_VM_DUMP
  cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 3);
#endif
  return ZX_OK;
}

#endif

zx_status_t VhmDevice::ReadInternal(uint8_t vcpu_id,
                                    uintptr_t addr,
                                    IoValue* value) const {
  if (shutdown_started_.load(std::memory_order_acquire)) {
    return ZX_ERR_CANCELED;
  }

  acrn_io_request* req = &ioreq_buf_->req_slot[vcpu_id];

  make_io_request(req, ACRN_IOREQ_DIR_READ, value->access_size, addr);
#ifndef _VHM_USE_CHANNEL_
  vhm_client_->WaitForIoRequestCompletion(vmid_, vcpu_id);
#else
  zx_status_t status = KickAndWaitIoRequest(vcpu_id, addr);
  if (status != ZX_OK) {
    return status;
  }
#endif

  memcpy(value->data, &req->reqs.mmio_request.value, value->access_size);
  return req->status;
}

zx_status_t VhmDevice::WriteInternal(uint8_t vcpu_id,
                                     uintptr_t addr,
                                     const IoValue& value) {
  if (shutdown_started_.load(std::memory_order_acquire)) {
    return ZX_ERR_CANCELED;
  }

  acrn_io_request* req = &ioreq_buf_->req_slot[vcpu_id];

  memcpy(&req->reqs.mmio_request.value, value.data, value.access_size);

  make_io_request(req, ACRN_IOREQ_DIR_WRITE, value.access_size, addr);
#ifndef _VHM_USE_CHANNEL_
  vhm_client_->WaitForIoRequestCompletion(vmid_, vcpu_id);
#else
  zx_status_t status = KickAndWaitIoRequest(vcpu_id, addr);
  if (status != ZX_OK) {
    return status;
  }
#endif

  if (req->status == ZX_ERR_PCI_BAR_REALLOC) {
    FXL_LOG(INFO) << "change pci bar address, old:" << std::hex
                  << req->reqs.bar_realloc.old_addr << ", new:" << std::hex
                  << req->reqs.bar_realloc.new_addr;
    zx_guest_remove_trap(guest_->handle(), ZX_GUEST_TRAP_MEM,
                         req->reqs.bar_realloc.old_addr);
    zx_guest_remove_trap(guest_->handle(), ZX_GUEST_TRAP_MEM,
                         req->reqs.bar_realloc.new_addr);
    this->OnNewMapping(req->reqs.bar_realloc.new_addr,
                       req->reqs.bar_realloc.size,
                       req->reqs.bar_realloc.type_type);
    req->status = ZX_OK;
  }

  return req->status;
}

#ifdef _VHM_USE_CHANNEL_

zx_status_t VhmDevice::WriteInternalAsync(uint8_t vcpu_id,
                                          uintptr_t addr,
                                          const IoValue& value) {
  acrn_io_request* req = &ioreq_buf_->req_slot[vcpu_id];
  uint8_t side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);

  utrace(TAG_COMM_ENTER, 1, 3, 3, side_chan, 1, addr);
  zx_status_t status = KickIoRequestAsync(vcpu_id, addr, req, value);
  utrace(TAG_COMM_EXIT, 1, 3, 3, side_chan, 1, addr);

  return status;
}

void VhmDevice::HandleVhmChannelMsgLoop() {
  zx_signals_t signals =
      ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | kSignalVhmShutdown;
  zx_status_t status;
  zx_signals_t pending = 0;
  zx_thread_set_priority(kIRQPriority);

  uint32_t cpu_mask = kNormalCpuAffinity;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                (void*)&cpu_mask);

  while (!shutdown_started_.load(std::memory_order_acquire)) {
    status = vhm_srv_chan_.wait_one(signals, zx::time::infinite(), &pending);
    if (status != ZX_OK) {
      if (!shutdown_started_.load(std::memory_order_acquire)) {
        FXL_LOG(ERROR) << "vhm channel wait failed, status:" << status;
      }
      break;
    }
    if (pending & kSignalVhmShutdown) {
      break;
    }

    if (pending & ZX_CHANNEL_READABLE) {
      struct vhm_chan_request request;
      uint32_t actual_bytes = 0;
      uint32_t msg_size = sizeof(request);
      uint64_t dev_id;
      uint64_t evt_id;
      uint8_t vcpu_id;
      uint8_t side_chan;
      status = vhm_srv_chan_.read(0, &request, msg_size, &actual_bytes, nullptr,
                                  0, nullptr);
      if (status != ZX_OK) {
        if (!shutdown_started_.load(std::memory_order_acquire)) {
          FXL_LOG(ERROR) << "Failed to read vhm message from vm: " << vmid_
                         << ", status:" << status;
        }
        break;
      }
      if (actual_bytes != msg_size) {
        FXL_LOG(ERROR) << "Wrong vhm message size, expect:[" << msg_size
                       << "], actual:[" << actual_bytes << "]";
        continue;
      }
      switch (request.cmd) {
        case VHM_INTR_INJECT:
          dev_id = request.param.intr_inject.dev_id;
          evt_id = request.param.intr_inject.evt_id;
#ifdef WITH_CROSS_VM_DUMP
          cvmd_tp_add_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 6, dev_id);
#endif
          if (evt_id == (uint64_t)-1) {
            utrace(TAG_COMM_ENTER, 1, 4, 1, dev_id);
            FXL_CHECK(interrupt_controller_ != nullptr);
            interrupt_controller_->Interrupt(dev_id);
#ifdef WITH_CROSS_VM_DUMP
            cvmd_tp_add_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 7, dev_id);
#endif
            utrace(TAG_COMM_EXIT, 1, 4, 1, dev_id);
          } else {
            FXL_CHECK(gic_its_ != nullptr);
            gic_its_->InjectMsi(dev_id, evt_id);
#ifdef WITH_CROSS_VM_DUMP
            cvmd_tp_add_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_H2G, 8, dev_id);
#endif
          }
#ifdef WITH_CROSS_VM_DUMP
          cvmd_cp_end(CROSS_VM_DUMP_VDEV_NOTIFY_H2G);
#endif
          break;
        case VHM_IO_REQUEST_ASYNC_RESPONSE:
#ifdef WITH_CROSS_VM_DUMP
          cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 13);
#endif
          vcpu_id = request.param.io_req.vcpu_id;
          side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);
          FXL_CHECK(side_chan >= 0 && side_chan < NUM_VHM_SIDEBAND_CHANNELS)
              << "vcpu id:" << vcpu_id;
          {
            utrace(TAG_COMM_ENTER, 1, 5, 3, side_chan);
            std::lock_guard<std::mutex> lock(side_chan_mutex_[side_chan]);
            side_chan_state_[side_chan] = 0;
            utrace(TAG_COMM_EXIT, 1, 5, 3, side_chan);
          }
          side_chan_cv_[side_chan].notify_all();
#ifdef WITH_CROSS_VM_DUMP
          cvmd_mp_set_ticks(CROSS_VM_DUMP_VDEV_NOTIFY_G2H, 14);
          cvmd_cp_end(CROSS_VM_DUMP_VDEV_NOTIFY_G2H);
#endif
          break;
        default:
          break;
      }
    }
    if (pending & ZX_CHANNEL_PEER_CLOSED) {
      break;
    }
  }
}

#endif

}  // namespace machina
