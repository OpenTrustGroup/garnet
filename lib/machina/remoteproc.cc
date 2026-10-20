// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoteproc.h"

#include "garnet/lib/machina/fdt_utils.h"
#include "lib/async/cpp/task.h"

namespace machina {

constexpr uint64_t kRprocDeviceMmioBase = 0xb00000000ULL;
constexpr uint64_t kRprocDeviceShmBase = 0xc00000000ULL;

static uint64_t alloc_mmio_address() {
  static int i = 0;
  auto mmio_base = kRprocDeviceMmioBase + i * PAGE_SIZE;
  i++;
  return mmio_base;
}

static uint64_t alloc_shm_address(size_t size) {
  static uint64_t next_base = kRprocDeviceShmBase;
  auto shm_base = next_base;
  next_base += size;
  return shm_base;
}

RemoteProcBase::RemoteProcBase(Guest* guest, RprocDeviceSvcSyncPtr svc)
    : guest_(guest),
      svc_(std::move(svc)),
      mmio_base_(alloc_mmio_address()),
      chan_waiter_(this) {
  RprocDeviceInfo info;
  svc_->GetInfo(&info);

  ctrl_irq_ = info.ctrl_irq;
  local_irqs_ = std::move(info.local_irqs);
  remote_irqs_ = std::move(info.remote_irqs);
  has_ipi_affinity_ = info.has_ipi_affinity;
  peer_vmid_ = info.peer_vmid;
  peer_online_ = false;
  vmo_ = std::move(info.shm_vmo);

  FXL_CHECK(!guest_->GetInterruptController()->IsPassthrough(ctrl_irq_))
            << "ctrl_irq: " << ctrl_irq_;

  auto status = vmo_.get_size(&shm_size_);
  FXL_CHECK(status == ZX_OK);

  shm_base_ = alloc_shm_address(shm_size_);
  status = guest_->MapPhysicalMemory(shm_base_, vmo_, /*writeable=*/true);
  FXL_CHECK(status == ZX_OK);

  status = guest_->CreateMapping(TrapType::MMIO_SYNC, mmio_base_, PAGE_SIZE, 0,
                                 this);
  FXL_CHECK(status == ZX_OK);

  async_ = loop_.async();
  status = loop_.StartThread();
  FXL_CHECK(status == ZX_OK);

  next_txid_ = std::make_unique<std::atomic<zx_txid_t>>();
  FXL_CHECK(next_txid_ != nullptr);
}

void RemoteProcBase::GetShmVmo(zx::vmo* out_vmo) {
  zx::vmo shm_vmo_duplicated;
  auto status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &shm_vmo_duplicated);
  FXL_CHECK(status == ZX_OK);

  *out_vmo = std::move(shm_vmo_duplicated);
}

void RemoteProcBase::notify_ctrl() {
  auto status = guest_->SignalInterrupt(/*mask=*/0x1, ctrl_irq_);
  FXL_CHECK(status == ZX_OK);
}

void RemoteProcBase::notify_vqueue(uint8_t queue_idx) {
  FXL_CHECK(queue_idx < local_irqs_.size());
  auto status = guest_->SignalInterrupt(/*mask=*/0x1, local_irqs_[queue_idx]);
  FXL_CHECK(status == ZX_OK);
}

void RemoteProcBase::peer_online() {
  svc_->GetChannel(&chan_);
  chan_waiter_.set_object(chan_.get());
  chan_waiter_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);

  auto status = chan_waiter_.Begin(async_);
  if (status != ZX_OK)
    FXL_LOG(WARNING) << "Failed to wait on channel: status=" << status;

  peer_online_ = true;
  notify_ctrl();
}
void RemoteProcBase::peer_offline() {
  chan_.reset();
  peer_online_ = false;
  state_ = RprocState::NOT_READY;
  peer_connect_state_ = RprocConnectState::NOT_CONNECT;
  notify_ctrl();
}

async_wait_result_t RemoteProcBase::HandleMsg(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_SHOULD_WAIT)
    return ASYNC_WAIT_AGAIN;

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "HandleMsg failed, status=" << status;
    return ASYNC_WAIT_FINISHED;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    RprocMsg msg;
    uint32_t actual_bytes = 0;
    uint32_t msg_size = sizeof(msg);
    status = chan_.read(0, &msg, msg_size, &actual_bytes, nullptr, 0, nullptr);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "failed to send message: " << status;
      return ASYNC_WAIT_AGAIN;
    }
    if (actual_bytes != msg_size) {
      FXL_LOG(WARNING) << "msg_size is incorrent: " << actual_bytes
                       << " != " << msg_size;
      return ASYNC_WAIT_AGAIN;
    }

    switch (msg.cmd) {
      case RprocCmd::SET_READY:
        state_ = RprocState::READY;
        notify_ctrl();
        break;
      case RprocCmd::CLEAR_READY:
        state_ = RprocState::NOT_READY;
        notify_ctrl();
        break;
      case RprocCmd::GET_SHM_BASE: {
        GetDaBaseReplyMsg reply;
        reply.tx_id = msg.tx_id;
        reply.da_base = shm_base_;
        SendMsg(&reply, sizeof(reply));
        break;
      }
      case RprocCmd::KICK: {
        int vq_idx = msg.arg;
        notify_vqueue(vq_idx);
        break;
      }
      case RprocCmd::CONNECT_STATE: {
        peer_connect_state_ = static_cast<RprocConnectState>(msg.arg);
        notify_ctrl();
        break;
      }

      default:
        FXL_CHECK(false) << "invalid cmd";
    }
  }

  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    return ASYNC_WAIT_FINISHED;
  }

  return ASYNC_WAIT_AGAIN;
}

zx_txid_t RemoteProcBase::GetNextTxid() const {
  zx_txid_t txid = 0;
  while (!txid) {
    txid = next_txid_->fetch_add(1, std::memory_order_relaxed);
  }
  return txid;
}

zx_status_t RemoteProcBase::PatchDeviceTree(const DeviceTreeSpec& dtb_spec,
                                            uintptr_t phys_base,
                                            const char* path,
                                            const char* compatible) {
  uintptr_t dtb_offset = dtb_spec.base - phys_base;
  size_t dtb_size = dtb_spec.size;

  void* dtb = guest_->phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  int rproc_offset = fdt_path_offset(dtb, path);
  if (rproc_offset >= 0) {
    fdt_pack(dtb);
    return ZX_OK;
  }
  int root_offset = fdt_path_offset(dtb, "/");
  check_status(root_offset, "root");
  const char* node_name = (path[0] == '/') ? path + 1 : path;
  rproc_offset = fdt_add_subnode(dtb, root_offset, node_name);
  check_status(rproc_offset, "rproc");

  std::vector<uint32_t> props;
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
  props.push_back(cpu_to_fdt32(ctrl_irq_ - 32));
  props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
  if (has_ipi_affinity_)
    props.push_back(cpu_to_fdt32(0));
  for (auto& irq : local_irqs_) {
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI));
    props.push_back(cpu_to_fdt32(irq - 32));
    props.push_back(cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI));
    if (has_ipi_affinity_)
      props.push_back(cpu_to_fdt32(0));
  }
  ret = fdt_setprop(dtb, rproc_offset, "interrupts", props.data(),
                    props.size() * sizeof(uint32_t));
  check_status(ret, "interrupts");

  std::vector<uint32_t> remote_irqs;
  for (auto& irq : remote_irqs_) {
    remote_irqs.push_back(cpu_to_fdt32(irq));
  }

  uint32_t remote_irq_count = remote_irqs.size();
  ret =
      fdt_setprop_u32(dtb, rproc_offset, "remote_irq_count", remote_irq_count);
  check_status(ret, "remote_irq_count");

  ret = fdt_setprop(dtb, rproc_offset, "remote_irqs", remote_irqs.data(),
                    remote_irqs.size() * sizeof(uint32_t));
  check_status(ret, "remote_irqs");

  uint64_t mmio_base = mmio_base_;
  uint64_t mmio_size = PAGE_SIZE;
  uint64_t shm_base = shm_base_;
  uint64_t shm_size = shm_size_;
  ret = fdt_setprop_cells_u64(dtb, rproc_offset, "reg", 4, mmio_base, mmio_size,
                              shm_base, shm_size);
  check_status(ret, "reg");

  ret = fdt_setprop_string(dtb, rproc_offset, "compatible", compatible);
  check_status(ret, "compatible");

  fdt_pack(dtb);

  return ZX_OK;
}

void RemoteProcBase::SendMsg(void* msg, size_t msg_size) {
  auto status = chan_.write(0, msg, msg_size, nullptr, 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "failed to send message: " << status;
  }
}

void RemoteProcBase::Call(void* msg,
                          size_t msg_size,
                          void* reply,
                          size_t reply_size) const {
  zx_channel_call_args_t args;
  args.wr_bytes = msg;
  args.wr_num_bytes = msg_size;
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = reply;
  args.rd_num_bytes = reply_size;
  args.rd_num_handles = 0;
  args.rd_handles = NULL;

  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status;
  auto status = chan_.call(0, zx::time::infinite(), &args, &bytes_read,
                           &handles_read, &read_status);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "failed to send message: " << status;
  }
}

uint64_t RemoteProcBase::get_shm_base() const {
  RprocMsg msg;
  msg.tx_id = GetNextTxid();
  msg.cmd = RprocCmd::GET_SHM_BASE;

  GetDaBaseReplyMsg reply;
  Call(&msg, sizeof(msg), &reply, sizeof(reply));
  return reply.da_base;
}

RprocHost::RprocHost(Guest* guest, RprocDeviceSvcSyncPtr svc)
    : RemoteProcBase(guest, std::move(svc)) {}

zx_status_t RprocHost::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case Regs::STATE:
      value->u32 = state_;
      break;

    case Regs::PEER_ONLINE:
      value->u32 = peer_online_;
      break;

    case Regs::PEER_VMID:
      value->u32 = peer_vmid_;
      break;

    case Regs::PEER_CONNECT_STATE:
      value->u32 = static_cast<uint32_t>(peer_connect_state_);
      break;

    default:
      return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t RprocHost::Write(uint64_t addr, const IoValue& value) {
  RprocMsg msg;

  switch (addr) {
    case Regs::SET_READY: {
      msg.cmd = RprocCmd::SET_READY;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    case Regs::CLEAR_READY: {
      msg.cmd = RprocCmd::CLEAR_READY;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    case Regs::KICK: {
      msg.cmd = RprocCmd::KICK;
      msg.arg = value.u32;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    case Regs::CONNECT_STATE: {
      msg.cmd = RprocCmd::CONNECT_STATE;
      msg.arg = value.u32;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    default:
      return ZX_ERR_IO;
  }

  return ZX_OK;
}

RprocRemote::RprocRemote(Guest* guest, RprocDeviceSvcSyncPtr svc)
    : RemoteProcBase(guest, std::move(svc)) {}

zx_status_t RprocRemote::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case Regs::GET_SHM_BASE_LOW: {
      value->u32 = (get_shm_base() & 0xffffffff);
      break;
    }

    case Regs::GET_SHM_BASE_HIGH: {
      value->u32 = (get_shm_base() >> 32);
      break;
    }

    case Regs::STATE:
      value->u32 = state_;
      break;

    case Regs::PEER_ONLINE:
      value->u32 = peer_online_;
      break;

    case Regs::PEER_VMID:
      value->u32 = peer_vmid_;
      break;

    case Regs::PEER_CONNECT_STATE:
      value->u32 = static_cast<uint32_t>(peer_connect_state_);
      break;

    default:
      return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t RprocRemote::Write(uint64_t addr, const IoValue& value) {
  RprocMsg msg;

  if (!chan_.is_valid())
    return ZX_OK;

  switch (addr) {
    case Regs::SET_READY: {
      msg.cmd = RprocCmd::SET_READY;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    case Regs::CLEAR_READY: {
      msg.cmd = RprocCmd::CLEAR_READY;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    case Regs::KICK: {
      msg.cmd = RprocCmd::KICK;
      msg.arg = value.u32;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    case Regs::CONNECT_STATE: {
      msg.cmd = RprocCmd::CONNECT_STATE;
      msg.arg = value.u32;
      SendMsg(&msg, sizeof(msg));
      break;
    }

    default:
      return ZX_ERR_IO;
  }

  return ZX_OK;
}

}  // namespace machina
