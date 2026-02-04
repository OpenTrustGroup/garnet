#include "garnet/lib/machina/vhm_device.h"

#ifdef _VHM_USE_CHANNEL_
#include "garnet/lib/machina/vhm_chan_message.h"
#endif

#include <acrn/acrn_common.h>
#include <acrn/atomic.h>
#include <fuchsia/cpp/machina.h>
#include <lib/app/cpp/application_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/zx/vmar.h>
#include <zircon/device/sysinfo.h>
#include <trusty_std.h>
#include <uapi/err.h>

#include <trace/event.h>
#include "utrace.h"

#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/vcpu.h"

#include <zircon/syscalls.h>

namespace machina {

thread_local size_t ThreadChannel = NUM_VHM_SIDEBAND_CHANNELS;

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

VhmDevice::VhmDevice(uint16_t vmid) : vmid_(vmid), binding_(this) {}

#ifdef _VHM_USE_CHANNEL_

static int vhm_thread_loop(void* param) {
  VhmDevice* device = (VhmDevice*)param;
  device->HandleVhmChannelMsgLoop();
  return 0;
}

#endif

zx_status_t VhmDevice::Init(Guest* guest,
                            InterruptController* interrupt_controller,
                            GicIts *gic_its) {
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

  thrd_t vhm_thread;
  int ret;
  ret = thrd_create_with_name(&vhm_thread, vhm_thread_loop, this, "vhm-uos-thread");
  FXL_CHECK(ret == thrd_success);
  ret = thrd_detach(vhm_thread);
  FXL_CHECK(ret == thrd_success);

  std::unique_ptr<uint8_t[]> state(new uint8_t[NUM_VHM_SIDEBAND_CHANNELS]);
  side_chan_state_ = std::move(state);

  std::unique_ptr<std::mutex[]> mutex(new std::mutex[NUM_VHM_SIDEBAND_CHANNELS]);
  side_chan_mutex_ = std::move(mutex);

  std::unique_ptr<std::condition_variable[]> cv(new std::condition_variable[NUM_VHM_SIDEBAND_CHANNELS]);
  side_chan_cv_ = std::move(cv);
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

  vhm_client_->RegisterInterruptListener(vmid_, binding_.NewBinding(loop_.async()), &err);
  FXL_CHECK(err == machina::VhmClientStatus::OK);
}

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
      FXL_CHECK(chn < NUM_VHM_SIDEBAND_CHANNELS) << "channel:" <<  chn;
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
    txid = next_txid_->fetch_add(1, std::memory_order_relaxed);
  }
  return txid;
}

void VhmDevice::KickAndWaitIoRequest(uint8_t vcpu_id, uintptr_t addr) const {
  struct vhm_chan_request request;
  struct vhm_chan_response response;
  zx_status_t status;
  utrace(TAG_COMM_ENTER, 1, 1, 2, vcpu_id);

  request.txid = GetNextTxid();
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
  zx_status_t read_status;
  status = vhm_cli_chan_.call(0, zx::time::infinite(), &args, &bytes_read,
                              &handles_read, &read_status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to kick vm:" << vmid_;;
  }
  utrace(TAG_COMM_EXIT, 1, 1, 2, vcpu_id);
}

void VhmDevice::KickIoRequestAsync(uint8_t vcpu_id,
                                   uintptr_t addr,
                                   acrn_io_request* req,
                                   const IoValue& value) const {
  struct vhm_chan_request request;
  zx_status_t status;
  uint8_t side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);
  std::mutex& mutex = side_chan_mutex_[side_chan];
  std::condition_variable& cv = side_chan_cv_[side_chan];
  {
    utrace(TAG_COMM_ENTER, 1, 2, 3, side_chan);
    std::unique_lock<std::mutex> lock(mutex);
    if (side_chan_state_[side_chan] != SIDE_CHAN_BUSY) {
      side_chan_state_[side_chan] = SIDE_CHAN_BUSY;
    } else {
      cv.wait(lock, [this, side_chan] {
        return side_chan_state_[side_chan] != SIDE_CHAN_BUSY;
      });
      side_chan_state_[side_chan] = SIDE_CHAN_BUSY;
    }
    utrace(TAG_COMM_EXIT, 1, 2, 3, side_chan);
  }

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
  }
}

#endif

zx_status_t VhmDevice::ReadInternal(uint8_t vcpu_id,
                                    uintptr_t addr,
                                    IoValue* value) const {
  acrn_io_request* req = &ioreq_buf_->req_slot[vcpu_id];

  make_io_request(req, ACRN_IOREQ_DIR_READ, value->access_size, addr);
#ifndef _VHM_USE_CHANNEL_
  vhm_client_->WaitForIoRequestCompletion(vmid_, vcpu_id);
#else
  KickAndWaitIoRequest(vcpu_id, addr);
#endif

  memcpy(value->data, &req->reqs.mmio_request.value, value->access_size);
  return req->status;
}

zx_status_t VhmDevice::WriteInternal(uint8_t vcpu_id,
                                     uintptr_t addr,
                                     const IoValue& value) {
  acrn_io_request* req = &ioreq_buf_->req_slot[vcpu_id];

  memcpy(&req->reqs.mmio_request.value, value.data, value.access_size);

  make_io_request(req, ACRN_IOREQ_DIR_WRITE, value.access_size, addr);
#ifndef _VHM_USE_CHANNEL_
  vhm_client_->WaitForIoRequestCompletion(vmid_, vcpu_id);
#else
  KickAndWaitIoRequest(vcpu_id, addr);
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
  KickIoRequestAsync(vcpu_id, addr, req, value);
  utrace(TAG_COMM_EXIT, 1, 3, 3, side_chan, 1, addr);

  return ZX_OK;
}

void VhmDevice::HandleVhmChannelMsgLoop() {
  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending = 0;
  zx_status_t status;
  zx_thread_set_priority(kIRQPriority);

  uint32_t cpu_mask = kNormalCpuAffinity;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
      (void*) &cpu_mask);

  while ((status = vhm_srv_chan_.wait_one(signals, zx::time::infinite(),
                                          &pending)) == ZX_OK) {
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
      FXL_CHECK(status == ZX_OK);
      FXL_CHECK(actual_bytes == msg_size);
      switch (request.cmd) {
        case VHM_INTR_INJECT:
          dev_id = request.param.intr_inject.dev_id;
          evt_id = request.param.intr_inject.evt_id;
          if (evt_id == (uint64_t)-1) {
            utrace(TAG_COMM_ENTER, 1, 4, 1, dev_id);
            FXL_CHECK(interrupt_controller_ != nullptr);
            interrupt_controller_->Interrupt(dev_id);
            utrace(TAG_COMM_EXIT, 1, 4, 1, dev_id);
          } else {
            FXL_CHECK(gic_its_ != nullptr);
            gic_its_->InjectMsi(dev_id, evt_id);
          }
          break;
        case VHM_IO_REQUEST_ASYNC_RESPONSE:
          vcpu_id = request.param.io_req.vcpu_id;
          side_chan = VCPU_ID_SIDEBAND_CHANNEL(vcpu_id);
          FXL_CHECK(side_chan >= 0 && side_chan < NUM_VHM_SIDEBAND_CHANNELS) << "vcpu id:" << vcpu_id;
          {
            utrace(TAG_COMM_ENTER, 1, 5, 3, side_chan);
            std::lock_guard<std::mutex> lock(side_chan_mutex_[side_chan]);
            side_chan_state_[side_chan] = 0;
            utrace(TAG_COMM_EXIT, 1, 5, 3, side_chan);
          }
          side_chan_cv_[side_chan].notify_all();
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
