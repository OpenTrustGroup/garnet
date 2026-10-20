// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc_channel.h"
#include <trusty_std.h>
#include <uapi/err.h>

#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/guest.h"

namespace machina {

constexpr size_t kIpcChannelSize = 4096;
constexpr uint64_t kIpcChannelBase = 0x800000000UL;

// static
uint8_t IpcChannel::next_chan_id_ = 0;

static inline uintptr_t get_channel_gpaddr(uint8_t id) {
  return kIpcChannelBase + id * kIpcChannelSize * 2;
}

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE |
    ZX_VM_FLAG_SPECIFIC;

// static
zx_status_t IpcChannel::Create(Guest* guest,
                               std::unique_ptr<IpcChannel>* out,
                               uint8_t way) {
  auto chan = std::make_unique<IpcChannel>(next_chan_id_, guest, way);
  if (chan == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  next_chan_id_++;
  *out = std::move(chan);
  return ZX_OK;
}

// static
zx_status_t IpcChannel::Create(Guest* guest,
                               zx::vmo tx_vmo,
                               zx::vmo rx_vmo,
                               std::unique_ptr<IpcChannel>* out) {
  auto chan = std::make_unique<IpcChannel>(
      next_chan_id_, guest, std::move(tx_vmo), std::move(rx_vmo));
  if (chan == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  next_chan_id_++;
  *out = std::move(chan);
  return ZX_OK;
}

IpcChannel::IpcChannel(uint8_t id, Guest* guest, uint8_t way)
    : id_(id), way_(way) {
  zx_status_t status = zx::vmo::create(kIpcChannelSize, 0, &tx_vmo_);
  FXL_CHECK(status == ZX_OK);

  status = zx::vmo::create(kIpcChannelSize, 0, &rx_vmo_);
  FXL_CHECK(status == ZX_OK);

  InitInternal(guest);
}

IpcChannel::IpcChannel(uint8_t id, Guest* guest, zx::vmo tx_vmo, zx::vmo rx_vmo)
    : id_(id) {
  tx_vmo_ = std::move(tx_vmo);
  rx_vmo_ = std::move(rx_vmo);

  InitInternal(guest);
}

void IpcChannel::InitInternal(Guest* guest) {
  tx_gpaddr_ = get_channel_gpaddr(id_);
  tx_size_ = kIpcChannelSize;
  rx_gpaddr_ = tx_gpaddr_ + kIpcChannelSize;
  rx_size_ = kIpcChannelSize;
  zx_paddr_t addr;
  zx_status_t status =
      zx::unowned_vmar::wrap(guest->vmar())
          .map(tx_gpaddr_, tx_vmo_, 0, tx_size_, kMapFlags, &addr);
  FXL_CHECK(status == ZX_OK);
  status = zx::unowned_vmar::wrap(guest->vmar())
               .map(rx_gpaddr_, rx_vmo_, 0, rx_size_, kMapFlags, &addr);
  FXL_CHECK(status == ZX_OK);
}

void IpcChannel::DuplicateVmoForPeer(zx::vmo* tx_vmo, zx::vmo* rx_vmo) {
  zx::vmo vmo;
  auto status = rx_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_CHECK(status == ZX_OK);
  *tx_vmo = std::move(vmo);
  status = tx_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_CHECK(status == ZX_OK);
  *rx_vmo = std::move(vmo);
}

void IpcRequestHandler::UpdateRxIrqStatusLocked() {
  for (uint8_t chan_id = 0; chan_id < kNumChannels; chan_id++) {
    uint64_t bitmask = (1UL << chan_id);
    if (chans_[chan_id]->is_rx_ready()) {
      rx_ready_status_ |= bitmask;
    }
  }
}

void IpcRequestHandler::UpdateTxIrqStatusLocked() {
  for (uint8_t chan_id = 0; chan_id < kNumChannels; chan_id++) {
    uint64_t bitmask = (1UL << chan_id);

    if (chans_[chan_id]->is_tx_done()) {
      tx_done_status_ |= bitmask;
    }
  }
}

void IpcRequestHandler::MaybeSendRxInterruptLocked() {
  zx_status_t status;
  if (rx_ready_status_) {
    if (!name_.empty())
      FXL_LOG(INFO) << name_ << ": TiggerRxIrq " << std::hex
                    << rx_ready_status_;
    status = guest_->SignalInterrupt(/*mask=*/0x1, rx_ready_irq_);
    FXL_CHECK(status == ZX_OK);
  }
}

void IpcRequestHandler::MaybeSendTxInterruptLocked() {
  zx_status_t status;
  if (tx_done_status_) {
    if (!name_.empty())
      FXL_LOG(INFO) << name_ << ": TiggerTxIrq " << std::hex << tx_done_status_;
    status = guest_->SignalInterrupt(/*mask=*/0x1, tx_done_irq_);
    FXL_CHECK(status == ZX_OK);
  }
}

void IpcRequestHandler::ClearRxReadyStatus(uint32_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  for (uint8_t chan_id = 0; chan_id < kNumChannels; chan_id++) {
    if (bitmask & (1UL << chan_id)) {
      chans_[chan_id]->clear_rx_ready();
    }
  }
  rx_ready_status_ &= ~bitmask;
}

void IpcRequestHandler::ClearTxDoneStatus(uint32_t bitmask) {
  fbl::AutoLock lock(&mutex_);
  for (uint8_t chan_id = 0; chan_id < kNumChannels; chan_id++) {
    if (bitmask & (1UL << chan_id)) {
      chans_[chan_id]->clear_tx_done();
    }
  }
  tx_done_status_ &= ~bitmask;
}

uint64_t IpcRequestHandler::GetRxReadyStatus() {
  fbl::AutoLock lock(&mutex_);
  return rx_ready_status_;
}

uint64_t IpcRequestHandler::GetTxDoneStatus() {
  fbl::AutoLock lock(&mutex_);
  return tx_done_status_;
}

zx_txid_t IpcRequestHandler::GetNextTxid() {
  zx_txid_t txid = 0;
  while (!txid) {
    txid = next_txid_.fetch_add(1, std::memory_order_relaxed);
  }
  return txid;
}

void IpcRequestHandler::NotifyRxReady(uint8_t chan_id) {
  struct fast_call_arg arg;
  arg.txid = GetNextTxid();
  arg.cmd = FAST_CALL_CMD_NOTIFY_RX_READY;
  arg.chan_id = chan_id;

  struct fast_call_rsp rsp;
  rsp.status = 0;

  zx_channel_call_args_t args;
  args.wr_bytes = &arg;
  args.wr_num_bytes = sizeof(arg);
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = &rsp;
  args.rd_num_bytes = sizeof(rsp);
  args.rd_num_handles = 0;
  args.rd_handles = NULL;

  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status;
  zx_status_t status;

  if (!name_.empty())
    FXL_LOG(INFO) << name_ << ": NotifyRxReady, chan_id=" << (uint32_t)chan_id;
  status = fast_client_.call(0, zx::deadline_after(zx::sec(5)), &args,
                             &bytes_read, &handles_read, &read_status);
  if (status == ZX_ERR_TIMED_OUT) {
    FXL_LOG(INFO) << "IpcRequestHandler->NotifyRxReady timeout.";
    return;
  }
}

void IpcRequestHandler::NotifyTxDone(uint8_t chan_id) {
  struct fast_call_arg arg;
  arg.txid = GetNextTxid();
  arg.cmd = FAST_CALL_CMD_NOTIFY_TX_DONE;
  arg.chan_id = chan_id;

  struct fast_call_rsp rsp;
  rsp.status = 0;

  zx_channel_call_args_t args;
  args.wr_bytes = &arg;
  args.wr_num_bytes = sizeof(arg);
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = &rsp;
  args.rd_num_bytes = sizeof(rsp);
  args.rd_num_handles = 0;
  args.rd_handles = NULL;

  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status;
  zx_status_t status;

  if (!name_.empty())
    FXL_LOG(INFO) << name_ << ": NotifyTxDone, chan_id=" << (uint32_t)chan_id;
  status = fast_client_.call(0, zx::deadline_after(zx::sec(5)), &args,
                             &bytes_read, &handles_read, &read_status);
  if (status == ZX_ERR_TIMED_OUT) {
    FXL_LOG(INFO) << "IpcRequestHandler->NotifyTxDone timeout.";
    return;
  }
}

int32_t IpcRequestHandler::VMCall(int vmid, zx_txid_t txid) {
  // TODO support multiple VMs.
  zx_status_t status;
  struct vm_call_arg arg;
  arg.txid = txid;

  struct vm_call_rsp rsp;
  rsp.status = 0;

  zx_channel_call_args_t args;
  args.wr_bytes = &arg;
  args.wr_num_bytes = sizeof(arg);
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = &rsp;
  args.rd_num_bytes = sizeof(rsp);
  args.rd_num_handles = 0;
  args.rd_handles = NULL;

  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status = -1;
  KickVMServer();
  status = vm_client_.call(0, zx::deadline_after(zx::sec(5)), &args,
                           &bytes_read, &handles_read, &read_status);
  if (status == ZX_ERR_TIMED_OUT) {
    FXL_LOG(INFO) << "IpcRequestHandler->VMCall timeout.";
    return ZX_ERR_TIMED_OUT;
  }
  FXL_CHECK(status == ZX_OK);

  return rsp.status;
}

zx_status_t IpcRequestHandler::VMRead(int vmid, zx_txid_t* txid) {
  fbl::AutoLock lock(&msg_lock_);
  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending = 0;
  struct vm_call_arg arg;
  uint32_t actual_bytes = 0;
  uint32_t msg_size = sizeof(arg);
  zx_status_t status;

  FXL_CHECK(txid);

  if (pending_msg_ <= 0) {
    // tell vm server wait for kick.
    return ZX_ERR_SHOULD_WAIT;
  }

  status =
      vm_server_.wait_one(signals, zx::deadline_after(zx::sec(5)), &pending);
  if (status == ZX_ERR_TIMED_OUT) {
    FXL_LOG(INFO) << "IpcRequestHandler->VMRead timeout.";
    return ZX_ERR_TIMED_OUT;
  }
  FXL_CHECK(status == ZX_OK);

  if (pending & ZX_CHANNEL_PEER_CLOSED) {
    // tell vm server wait for kick.
    return ZX_ERR_SHOULD_WAIT;
  }

  FXL_CHECK(pending & ZX_CHANNEL_READABLE);

  status =
      vm_server_.read(0, &arg, msg_size, &actual_bytes, nullptr, 0, nullptr);
  if (status == ZX_OK) {
    FXL_CHECK(actual_bytes == msg_size);
    *txid = arg.txid;
    pending_msg_--;
    FXL_CHECK(pending_msg_ >= 0);
  } else {
    // tell vm server retry.
    status = ZX_ERR_IO;
  }

  return status;
}

zx_status_t IpcRequestHandler::VMWrite(int vmid,
                                       zx_txid_t txid,
                                       int32_t result) {
  // TODO support multiple VMs.
  zx_status_t status;
  struct vm_call_rsp rsp;
  rsp.txid = txid;
  rsp.status = result;
  zx_signals_t signals = ZX_CHANNEL_WRITABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending = 0;

  status =
      vm_server_.wait_one(signals, zx::deadline_after(zx::sec(5)), &pending);
  if (status == ZX_ERR_TIMED_OUT) {
    FXL_LOG(INFO) << "IpcRequestHandler->VMWrite timeout.";
    return ZX_ERR_TIMED_OUT;
  }
  FXL_CHECK(status == ZX_OK);

  if (pending & ZX_CHANNEL_PEER_CLOSED) {
    // tell vm server wait for kick.
    return ZX_ERR_SHOULD_WAIT;
  }

  FXL_CHECK(pending & ZX_CHANNEL_WRITABLE);

  status = vm_server_.write(0, &rsp, sizeof(rsp), NULL, 0);

  return status;
}

void IpcRequestHandler::SetVMServerState(int vmid, int server_id, int state) {
  // TODO support multiple vm servers.
  vm_server_state_ = state;
}

void IpcRequestHandler::OnRxReady(uint8_t chan_id, OnRxReadyCallback callback) {
  fbl::AutoLock lock(&mutex_);
  if (!name_.empty())
    FXL_LOG(INFO) << name_ << ": OnRxReady, chan_id=" << (uint32_t)chan_id;
  chans_[chan_id]->set_rx_ready();
  UpdateRxIrqStatusLocked();
  MaybeSendRxInterruptLocked();
  if (callback) {
    callback();
  }
}

void IpcRequestHandler::OnTxDone(uint8_t chan_id, OnTxDoneCallback callback) {
  fbl::AutoLock lock(&mutex_);
  if (!name_.empty())
    FXL_LOG(INFO) << name_ << ": OnTxDone: chan_id=" << (uint32_t)chan_id;
  chans_[chan_id]->set_tx_done();
  UpdateTxIrqStatusLocked();
  MaybeSendTxInterruptLocked();
  if (callback) {
    callback();
  }
}

zx_status_t IpcRequestHandler::CreateMboxDeviceTreeNodes(
    const DeviceTreeSpec& dtb_spec,
    uintptr_t phys_base,
    uint32_t node) {
  uintptr_t dtb_offset = dtb_spec.base - phys_base;
  size_t dtb_size = dtb_spec.size;
  uint32_t cells_size = 3;

  // Validate device tree.
  void* dtb = guest_->phys_mem().as<void>(dtb_offset, dtb_size);

  int ret = fdt_open_into(dtb, dtb, dtb_size);
  if (ret < 0) {
    FXL_LOG(INFO) << "Invalid device tree";
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  guest_->GetGICInterruptCellSize(dtb, cells_size);

  std::string nebula_mbox = "/nebula-mbox@";
  nebula_mbox += std::to_string(node);

  int mbox_offset = fdt_path_offset(dtb, nebula_mbox.c_str());
  if (mbox_offset < 0) {
    nebula_mbox = "/nebula_mbox@";
    nebula_mbox += std::to_string(node);
    mbox_offset = fdt_path_offset(dtb, nebula_mbox.c_str());
  }
  if (mbox_offset < 0) {
    FXL_LOG(WARNING) << "nebula_mbox/nebula-mbox is not supported, skipped";
    fdt_pack(dtb);
    return ZX_OK;
  }

  // clang-format off
  uint32_t props[] = {cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(tx_done_irq_ - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0),
                      cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI),
                      cpu_to_fdt32(rx_ready_irq_ - 32),
                      cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI),
                      cpu_to_fdt32(0),};
  // clang-format on
  if (cells_size == 3) {
    props[3] = cpu_to_fdt32(GIC_FDT_IRQ_TYPE_SPI);
    props[4] = cpu_to_fdt32(rx_ready_irq_ - 32);
    props[5] = cpu_to_fdt32(GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
  }
  ret = fdt_setprop(dtb, mbox_offset, "interrupts", props,
                    2 * cells_size * sizeof(uint32_t));
  check_status(ret, "interrupts");

  for (uint8_t i = 0; i < kNumChannels; i++) {
    char name[8];
    snprintf(name, sizeof(name), "chan%d", i);
    int chan_offset = fdt_add_subnode(dtb, mbox_offset, name);
    check_status(chan_offset, name);

    auto& chan = chans_[i];
    FXL_CHECK(chan != nullptr);

    uint8_t id = i;
    ret = fdt_setprop(dtb, chan_offset, "id", &id, sizeof(id));
    check_status(ret, "id");

    auto tx_base = chan->tx_gpaddr();
    auto tx_size = chan->tx_size();
    auto rx_base = chan->rx_gpaddr();
    auto rx_size = chan->rx_size();
    ret = fdt_setprop_cells_u64(dtb, chan_offset, "reg", 4, tx_base, tx_size,
                                rx_base, rx_size);
    check_status(ret, "reg");

    ret =
        fdt_setprop_string(dtb, chan_offset, "compatible", "grt,ipc-mbox-chan");
    check_status(ret, "compatible");
  }

  fdt_pack(dtb);
  return ZX_OK;
}

void IpcRequestHandler::FastCallLoop() {
  zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  zx_signals_t pending = 0;
  zx_status_t status;

  zx_thread_set_priority(kIRQPriority);

  uint32_t cpu_mask = kNormalCpuAffinity;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                (void*)&cpu_mask);

  while ((status = fast_server_.wait_one(signals, zx::time::infinite(),
                                         &pending)) == ZX_OK) {
    if (pending & ZX_CHANNEL_READABLE) {
      struct fast_call_arg arg;
      struct fast_call_rsp rsp;
      uint32_t actual_bytes = 0;
      uint32_t msg_size = sizeof(arg);
      status = fast_server_.read(0, &arg, msg_size, &actual_bytes, nullptr, 0,
                                 nullptr);

      FXL_CHECK(status == ZX_OK);
      FXL_CHECK(actual_bytes == msg_size);

      switch (arg.cmd) {
        case FAST_CALL_CMD_NOTIFY_RX_READY:
          OnRxReady(arg.chan_id, NULL);
          rsp.status = 0;
          break;
        case FAST_CALL_CMD_NOTIFY_TX_DONE:
          OnTxDone(arg.chan_id, NULL);
          rsp.status = 0;
          break;
        case FAST_CALL_CMD_KICK_SERVER:
          pending_msg_++;
          OnKickVMServer();
          rsp.status = 0;
          break;
        case FAST_CALL_CMD_SET_POWER_STATE:
          SetPowerState(arg.chan_id, arg.mbox_state);
          rsp.status = 0;
          break;
        default:
          rsp.status = -1;
      }
      rsp.txid = arg.txid;

      fast_server_.write(0, &rsp, sizeof(rsp), NULL, 0);
    } else if (pending & ZX_CHANNEL_PEER_CLOSED) {
      break;
    }
  }
}

void IpcRequestHandler::KickVMServer() {
  struct fast_call_arg arg;
  arg.txid = GetNextTxid();
  arg.cmd = FAST_CALL_CMD_KICK_SERVER;

  struct fast_call_rsp rsp;
  rsp.status = 0;

  zx_channel_call_args_t args;
  args.wr_bytes = &arg;
  args.wr_num_bytes = sizeof(arg);
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = &rsp;
  args.rd_num_bytes = sizeof(rsp);
  args.rd_num_handles = 0;
  args.rd_handles = NULL;

  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status;
  zx_status_t status;

  status = fast_client_.call(0, zx::deadline_after(zx::sec(5)), &args,
                             &bytes_read, &handles_read, &read_status);
  if (status == ZX_ERR_TIMED_OUT) {
    FXL_LOG(INFO) << "IpcRequestHandler->KickVMServer timeout.";
    return;
  }
}

void IpcRequestHandler::OnKickVMServer() {
  if (vm_server_state_ != VM_SERVER_STATE_RUNNING) {
    OnRxReady(0, NULL);
  }
}

void IpcRequestHandler::SetSchedMem() {
  if (sched_mem_pa_ && sched_mem_sz_)
    guest_->SetSchedMem(sched_mem_pa_, sched_mem_sz_);
}

uint32_t IpcRequestHandler::GetMailboxPowerState(uint32_t vmid) {
  fbl::AutoLock lock(&mutex_);
  if (IsAlpsVmid(vmid)) {
    return mbox_state_vm0_;
  } else {
    return mbox_state_vm1_;
  }
}

void IpcRequestHandler::SetPowerState(uint32_t vmid, uint32_t state) {
  fbl::AutoLock lock(&mutex_);
  if (IsAlpsVmid(vmid)) {
    mbox_state_vm0_ = state;
  } else {
    mbox_state_vm1_ = state;
  }
}

void IpcRequestHandler::SetMailboxPowerState(uint32_t vmid, uint32_t state) {
  struct fast_call_arg arg;
  arg.txid = GetNextTxid();
  arg.cmd = FAST_CALL_CMD_SET_POWER_STATE;
  arg.chan_id = vmid;
  arg.mbox_state = state;
  struct fast_call_rsp rsp;
  rsp.status = 0;
  zx_channel_call_args_t args;
  args.wr_bytes = &arg;
  args.wr_num_bytes = sizeof(arg);
  args.wr_handles = NULL;
  args.wr_num_handles = 0;
  args.rd_bytes = &rsp;
  args.rd_num_bytes = sizeof(rsp);
  args.rd_num_handles = 0;
  args.rd_handles = NULL;
  uint32_t bytes_read;
  uint32_t handles_read;
  zx_status_t read_status;
  fast_client_.call(0, zx::time::infinite(), &args, &bytes_read,
      &handles_read, &read_status);
}

void IpcRequestHandler::SetMailboxSuspend(uint32_t vmid, uint32_t state) {
  SetMailboxPowerState(vmid, state);
}

void IpcRequestHandler::SetMailboxResume(uint32_t vmid, uint32_t state) {
  SetMailboxPowerState(vmid, state);
}

}  // namespace machina
