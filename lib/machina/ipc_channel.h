// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fuchsia/cpp/machina.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <array>

#include "garnet/lib/machina/guest_config.h"

namespace machina {

#define FAST_CALL_CMD_INVALID (0)
#define FAST_CALL_CMD_NOTIFY_RX_READY (0x1)
#define FAST_CALL_CMD_NOTIFY_TX_DONE  (0x2)
#define FAST_CALL_CMD_KICK_SERVER     (0x3)
#define FAST_CALL_CMD_SET_POWER_STATE (0x4)

#define VM_SERVER_STATE_STOPPED (0)
#define VM_SERVER_STATE_RUNNING (1)
#define VM_SERVER_STATE_PAUSING (2)
#define VM_SERVER_STATE_PAUSED (3)

struct fast_call_arg {
  zx_txid_t txid;
  uint32_t cmd;
  uint32_t mbox_state;
  union {
    uint32_t chan_id;
  };
};

struct fast_call_rsp {
  zx_txid_t txid;
  int32_t status;
};

struct vm_call_arg {
  zx_txid_t txid;
};

struct vm_call_rsp {
  zx_txid_t txid;
  int32_t status;
};

constexpr uint8_t kNumChannels = 5;

class Guest;
class IpcChannel {
 public:
  static zx_status_t Create(Guest* guest,
                            std::unique_ptr<IpcChannel>* out,
                            uint8_t way);
  static zx_status_t Create(Guest* guest,
                            zx::vmo tx_vmo,
                            zx::vmo rx_vmo,
                            std::unique_ptr<IpcChannel>* out);

  IpcChannel(uint8_t id, Guest* guest, uint8_t way);
  IpcChannel(uint8_t id, Guest* guest, zx::vmo tx_vmo, zx::vmo rx_vmo);

  void DuplicateVmoForPeer(zx::vmo* tx_vmo, zx::vmo* rx_vmo);
  uint8_t id() { return id_; }

  void set_rx_ready() { rx_ready_ = true; }
  void clear_rx_ready() { rx_ready_ = false; }
  void set_tx_done() { tx_done_ = true; }
  void clear_tx_done() { tx_done_ = false; }
  bool is_rx_ready() { return rx_ready_; }
  bool is_tx_done() { return tx_done_; }

  uintptr_t tx_gpaddr() { return tx_gpaddr_; }
  uintptr_t tx_size() { return tx_size_; }
  uintptr_t rx_gpaddr() { return rx_gpaddr_; }
  uintptr_t rx_size() { return rx_size_; }
  static void reset_next_chan_id(void);
  void set_way(uint8_t way) { way_ = way; }

 private:
  void InitInternal(Guest* guest);

  zx::vmo tx_vmo_;
  zx::vmo rx_vmo_;
  uintptr_t tx_gpaddr_ = 0;
  size_t tx_size_ = 0;
  uintptr_t rx_gpaddr_ = 0;
  size_t rx_size_ = 0;
  bool rx_ready_ = false;
  bool tx_done_ = false;

  uint8_t id_;
  uint8_t way_;
  static uint8_t next_chan_id_;
};

class IpcRequestHandler : public IpcEventListener {
 public:
  IpcRequestHandler(Guest* guest, const IpcMboxSpec& spec, int mailbox_way)
      : guest_(guest),
        rx_ready_irq_(spec.rx_ready_irq),
        tx_done_irq_(spec.tx_done_irq),
        mailbox_way_(mailbox_way),
        pending_msg_(0),
        event_listener_binding_(this) {}

  IpcRequestHandler(Guest* guest)
      : guest_(guest), pending_msg_(0), event_listener_binding_(this) {}

  zx_txid_t GetNextTxid();
  void NotifyRxReady(uint8_t chan_id);
  void NotifyTxDone(uint8_t chan_id);
  int32_t VMCall(int vmid, zx_txid_t txid);
  zx_status_t VMRead(int vmid, zx_txid_t* txid);
  zx_status_t VMWrite(int vmid, zx_txid_t txid, int32_t result);
  void SetVMServerState(int vmid, int server_id, int state);
  uint64_t GetRxReadyStatus();
  uint64_t GetTxDoneStatus();
  void ClearRxReadyStatus(uint32_t bitmask);
  void ClearTxDoneStatus(uint32_t bitmask);
  zx_status_t CreateMboxDeviceTreeNodes(const DeviceTreeSpec& dtb_spec,
                                        uintptr_t phys_base,
                                        uint32_t node);
  void FastCallLoop();
  void KickVMServer();
  void OnKickVMServer();
  // void SetTraceMem();
  void SetSchedMem();

  void SetPowerState(uint32_t vmid, uint32_t state);
  void SetMailboxPowerState(uint32_t vmid, uint32_t state);
  uint32_t GetMailboxPowerState(uint32_t vmid);
  void SetMailboxSuspend(uint32_t vmid, uint32_t state);
  void SetMailboxResume(uint32_t vmid, uint32_t state);

  // |IpcEventListener|
  virtual void OnRxReady(uint8_t chan_id, OnRxReadyCallback callback) override;
  virtual void OnTxDone(uint8_t chan_id, OnTxDoneCallback callback) override;

  void set_name(std::string name) { name_ = name; }
  void set_trace_mem(uint64_t addr, uint64_t size) {
    trace_mem_pa_ = addr;
    trace_mem_sz_ = size;
  }
  void set_sched_mem(uint64_t addr, uint64_t size) {
    sched_mem_pa_ = addr;
    sched_mem_sz_ = size;
  }

  std::array<std::unique_ptr<IpcChannel>, kNumChannels>& chan_array(void) {
    return chans_;
  }

  uint16_t get_rx_ready_irq(void) { return rx_ready_irq_; }
  void set_rx_ready_irq(uint16_t rx_ready_irq) { rx_ready_irq_ = rx_ready_irq; }
  uint16_t get_tx_done_irq(void) { return tx_done_irq_; }
  void set_tx_done_irq(uint16_t tx_done_irq) { tx_done_irq_ = tx_done_irq; }
  uint64_t get_trace_mem_pa(void) { return trace_mem_pa_; }
  uint64_t get_trace_mem_sz(void) { return trace_mem_sz_; }
  void set_fast_client(zx::channel fast_client) {
    fast_client_ = std::move(fast_client);
  }
  void set_fast_server(zx::channel fast_server) {
    fast_server_ = std::move(fast_server);
  }
  void set_vm_client(zx::channel vm_client) {
    vm_client_ = std::move(vm_client);
  }
  void set_vm_server(zx::channel vm_server) {
    vm_server_ = std::move(vm_server);
  }
  IpcEventListenerSyncPtr& getIpcEventListernerPtr(void) { return peer_; }
  fidl::Binding<IpcEventListener>& getIpcEventener(void) {
    return event_listener_binding_;
  };
  int get_mailbox_way(void) { return mailbox_way_; }
  int get_mailbox_vmid(void) { return vmid_; }
  void set_mailbox_vmid(int vmid) { vmid_ = vmid; }
  uint64_t get_sched_mem_pa(void) { return sched_mem_pa_; };
  uint64_t get_sched_mem_sz(void) { return sched_mem_sz_; };

 protected:
  Guest* guest_;
  uint16_t rx_ready_irq_ = 0;
  uint16_t tx_done_irq_ = 0;
  uint32_t mbox_state_vm0_ = 0;
  uint32_t mbox_state_vm1_ = 0;
  uint64_t trace_mem_pa_ = 0;
  uint64_t trace_mem_sz_ = 0;
  uint64_t sched_mem_pa_ = 0;
  uint64_t sched_mem_sz_ = 0;
  uint64_t rx_ready_status_ = 0;
  uint64_t tx_done_status_ = 0;
  int vmid_ = -1;
  int mailbox_way_ = -1;
  fbl::Mutex mutex_;
  std::array<std::unique_ptr<IpcChannel>, kNumChannels> chans_;

  IpcEventListenerSyncPtr peer_;
  zx::channel fast_client_;
  zx::channel fast_server_;
  zx::channel vm_client_;
  zx::channel vm_server_;
  int vm_server_state_;
  std::atomic<zx_txid_t> next_txid_;
  std::atomic<int> pending_msg_;
  fbl::Mutex msg_lock_;
  fidl::Binding<IpcEventListener> event_listener_binding_;

  std::string name_;

  void UpdateRxIrqStatusLocked();
  void UpdateTxIrqStatusLocked();
  void MaybeSendRxInterruptLocked();
  void MaybeSendTxInterruptLocked();
};
}  // namespace machina
