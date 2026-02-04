// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/guest/proto/rproc.pb.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/remoteproc_vm_monitor.h"

#include "lib/async-loop/cpp/loop.h"
#include "lib/async/cpp/wait.h"

#include "lib/fxl/logging.h"
#include "lib/zx/channel.h"
#include "lib/zx/vmar.h"
#include "lib/zx/vmo.h"

namespace machina {

enum RprocCmd {
  SET_READY,
  CLEAR_READY,
  GET_SHM_BASE,
  KICK,
};

struct RprocMsg {
  zx_txid_t tx_id;
  enum RprocCmd cmd;
  uint32_t arg;
};

struct GetDaBaseReplyMsg {
  zx_txid_t tx_id;
  uint64_t da_base;
};

enum RprocState {
  NOT_READY,
  READY,
};

class RprocManager;
class RemoteProcBase : public IoHandler, public PeerStateNotifier {
 public:
  RemoteProcBase(Guest* guest, RprocDeviceSvcSyncPtr svc);

  zx_handle_t vmo() { return vmo_.get(); }

  zx_status_t PatchDeviceTree(const DeviceTreeSpec& dtb_spec,
                              uintptr_t phys_base,
                              const char* path,
                              const char* compatible);

  void GetShmVmo(zx::vmo* out_vmo);
  void set_remote_irqs(std::vector<uint16_t> remote_irqs) {
    remote_irqs_ = std::move(remote_irqs);
  };
  std::vector<uint16_t>& local_irqs() { return local_irqs_; }
  std::vector<uint16_t>& remote_irqs() { return remote_irqs_; }
  uint16_t ctrl_irq() { return ctrl_irq_; }
  bool has_ipi_affinity() { return has_ipi_affinity_; }

  int16_t peer_vmid() override { return peer_vmid_; }
  bool is_connected() override { return chan_.is_valid(); }
  void peer_online() override;
  void peer_offline() override;

 protected:
  void notify_ctrl();
  void notify_vqueue(uint8_t queue_idx);
  void notify(uint8_t idx);
  void SendMsg(void* msg, size_t msg_size);
  void Call(void* msg, size_t msg_size, void* reply, size_t reply_size) const;
  Guest* guest_;
  RprocDeviceSvcSyncPtr svc_;

  zx::vmo vmo_;
  uint64_t mmio_base_;
  uint64_t shm_base_;
  size_t shm_size_;
  RprocState state_ = RprocState::NOT_READY;

  uint16_t ctrl_irq_;
  std::vector<uint16_t> local_irqs_;
  std::vector<uint16_t> remote_irqs_;
  zx::channel chan_;
  int16_t peer_vmid_;
  bool peer_online_;

  bool has_ipi_affinity_;

  async_wait_result_t HandleMsg(async_t* async,
                                zx_status_t status,
                                const zx_packet_signal_t* signal);
  async::WaitMethod<RemoteProcBase, &RemoteProcBase::HandleMsg> chan_waiter_;
  async::Loop loop_;
  async_t* async_;

  zx_txid_t GetNextTxid() const;
  std::unique_ptr<std::atomic<zx_txid_t>> next_txid_;

  uint64_t get_shm_base() const;
};

class RprocHost : public RemoteProcBase {
 public:
  RprocHost(Guest* guest, RprocDeviceSvcSyncPtr svc);

 private:
  enum Regs {
    SET_READY = 0x0,
    CLEAR_READY = 0x4,
    STATE = 0x8,
    KICK = 0xc,
    PEER_VMID = 0x10,
    PEER_ONLINE = 0x14,
  };

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
};

class RprocRemote : public RemoteProcBase {
 public:
  RprocRemote(Guest* guest, RprocDeviceSvcSyncPtr svc);

 private:
  enum Regs {
    SET_READY = 0x0,
    CLEAR_READY = 0x4,
    GET_SHM_BASE_LOW = 0x8,
    GET_SHM_BASE_HIGH = 0xc,
    STATE = 0x10,
    KICK = 0x14,
    PEER_VMID = 0x18,
    PEER_ONLINE = 0x1C,
  };

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
};
}  // namespace machina

class RprocClient {
 public:
  using RprocRemoteList = std::vector<std::unique_ptr<machina::RprocRemote>>;
  using RprocHostList = std::vector<std::unique_ptr<machina::RprocHost>>;
  RprocClient(uint16_t vmid,
              machina::RprocService* mgr,
              machina::Guest& guest,
              machina::InterruptController& interrupt_controller)
      : binding_(mgr), monitor_(vmid, loop_.async()) {
    rproc_svc_.Bind(binding_.NewBinding(loop_.async()));

    auto status = loop_.StartThread();
    FXL_CHECK(status == ZX_OK);

    GetDevices(vmid, guest, interrupt_controller);
    RegisterVmStateListener(vmid);
  }

  RprocClient(uint16_t vmid,
              machina::RprocServiceSyncPtr rproc_svc,
              machina::Guest& guest,
              machina::InterruptController& interrupt_controller)
      : binding_(nullptr), rproc_svc_(std::move(rproc_svc)), monitor_(vmid, loop_.async()) {
    auto status = loop_.StartThread();
    FXL_CHECK(status == ZX_OK);

    GetDevices(vmid, guest, interrupt_controller);
    RegisterVmStateListener(vmid);
  }

  void GetDevices(uint16_t vmid,
                  machina::Guest& guest,
                  machina::InterruptController& interrupt_controller) {
    fidl::VectorPtr<fidl::InterfaceHandle<machina::RprocDeviceSvc>> handles;
    rproc_svc_->GetRemoteDevices(vmid, &handles);
    for (auto& handle : *handles) {
      machina::RprocDeviceSvcSyncPtr svc;
      svc.Bind(std::move(handle));

      auto remote_dev =
          std::make_unique<machina::RprocRemote>(&guest, std::move(svc));
      FXL_CHECK(remote_dev != nullptr);

#if defined(RPROC_NOTIFY_WITH_PHYS_IRQ)
      auto status =
          interrupt_controller.PassThroughInterrupts(remote_dev->local_irqs());
      FXL_CHECK(status == ZX_OK);
#endif

      remote_devs_.push_back(std::move(remote_dev));
    }

    rproc_svc_->GetHostDevices(vmid, &handles);
    for (auto& handle : *handles) {
      machina::RprocDeviceSvcSyncPtr svc;
      svc.Bind(std::move(handle));

      auto host_dev =
          std::make_unique<machina::RprocHost>(&guest, std::move(svc));
      FXL_CHECK(host_dev != nullptr);

#if defined(RPROC_NOTIFY_WITH_PHYS_IRQ)
      auto status =
          interrupt_controller.PassThroughInterrupts(host_dev->local_irqs());
      FXL_CHECK(status == ZX_OK);
#endif
      host_devs_.push_back(std::move(host_dev));
    }
  }

  void PatchDeviceTree(const machina::DeviceTreeSpec& dtb, uint64_t phys_base) {
    int dev_idx = 0;
    char path[256];
    zx_status_t status;

    for (auto& remote_dev : remote_devs_) {
      snprintf(path, 256, "/nebula_rproc_remote%d", dev_idx++);
      status =
          remote_dev->PatchDeviceTree(dtb, phys_base, path, "grt,rproc-remote");
      FXL_CHECK(status == ZX_OK);
    }

    dev_idx = 0;
    for (auto& host_dev : host_devs_) {
      snprintf(path, 256, "/nebula_rproc_host%d", dev_idx++);
      status =
          host_dev->PatchDeviceTree(dtb, phys_base, path, "grt,rproc-host");
      FXL_CHECK(status == ZX_OK);
    }
  }

  void RegisterVmStateListener(int16_t vmid) {
    for (auto& dev : remote_devs_)
      monitor_.add_notifier(dev.get());
    for (auto& dev : host_devs_)
      monitor_.add_notifier(dev.get());
    rproc_svc_->RegisterVmStateListener(vmid, monitor_.NewBinding());
  }

 private:
  fidl::Binding<machina::RprocService> binding_;
  RprocRemoteList remote_devs_;
  RprocHostList host_devs_;
  machina::RprocServiceSyncPtr rproc_svc_;
  async::Loop loop_;
  machina::RprocVmStateMonitor monitor_;
};