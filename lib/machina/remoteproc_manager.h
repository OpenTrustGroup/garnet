// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "garnet/bin/guest/proto/rproc.pb.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/vm_id.h"
#include "lib/app/cpp/application_context.h"
#include "lib/async/cpp/task.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class RprocManager : public RprocService {
 public:
  class RemoteDevice;
  class HostDevice;
  using RemoteDeviceList = std::vector<std::unique_ptr<RemoteDevice>>;
  using HostDeviceList = std::vector<std::unique_ptr<HostDevice>>;

  using SharedMemoryAllocator = std::function<zx::vmo(std::string&)>;

  class RprocDeviceBase : public RprocDeviceSvc {
   public:
    RprocDeviceBase() : binding_(this) {}

    fidl::InterfaceHandle<RprocDeviceSvc> NewBinding(async_t* async = nullptr) {
      return binding_.NewBinding(async);
    }
    int16_t vmid() { return vmid_; }
    int16_t peer_vmid() { return peer_vmid_; }

    void Bind(zx::channel chan) {
      chan_ = std::move(chan);
      connected_ = true;
    }

    void Unbind() {
      chan_.reset();
      connected_ = false;
    }

    void NotifyVmOnline(int16_t vmid) {
      if (vmid_ == vmid)
        online_ = true;

      if (!connected_ && (online_ && peer_->online_)) {
        CreateChannel();
      }
      FXL_LOG(INFO) << "NotifyVmOnline vmid=" << vmid << ", online=" << online_
                    << ", peer_online=" << peer_->online_
                    << ", connected=" << connected_ << ", local=" << vmid_
                    << ", peer=" << peer_->vmid_;
    }

    void NotifyVmOffline(int16_t vmid) {
      if (vmid_ == vmid)
        online_ = false;

      if (connected_ && (!online_ || !peer_->online_)) {
        FXL_LOG(INFO) << "Disconnected: VM " << vmid_ << " <-> VM "
                      << peer_->vmid_;
        Unbind();
        peer_->Unbind();
      }
      FXL_LOG(INFO) << "NotifyVmOffline vmid=" << vmid << ", online=" << online_
                    << ", peer_online=" << peer_->online_
                    << ", connected=" << connected_ << ", local=" << vmid_
                    << ", peer=" << peer_->vmid_;
    }

    void CreateChannel() {
      zx::channel h1, h2;
      auto status = zx::channel::create(/*flags=*/0, &h1, &h2);
      FXL_CHECK(status == ZX_OK);
      FXL_LOG(INFO) << "Connected: VM " << vmid_ << " <-> VM " << peer_->vmid_;

      Bind(std::move(h1));
      peer_->Bind(std::move(h2));
    }

    virtual void GetChannel(GetChannelCallback callback) override {
      callback(std::move(chan_));
    }

   protected:
    fidl::Binding<RprocDeviceSvc> binding_;
    int16_t vmid_;
    int16_t peer_vmid_;
    bool online_;
    bool connected_;
    RprocDeviceBase* peer_;
    zx::channel chan_;
  };

  class HostDevice : public RprocDeviceBase {
   public:
    HostDevice() {}

    virtual void GetInfo(GetInfoCallback callback) override {
      auto remote = reinterpret_cast<RemoteDevice*>(peer_);

      RprocDeviceInfo info;
      info.ctrl_irq = remote->ctrl_irq();
      info.has_ipi_affinity = has_ipi_affinity_;
      info.peer_vmid = peer_vmid_;
      remote->GetShmVmo(&info.shm_vmo);

#if defined(RPROC_NOTIFY_WITH_PHYS_IRQ)
      info.local_irqs = fidl::VectorPtr<uint16_t>(remote->remote_irqs());
      info.remote_irqs = fidl::VectorPtr<uint16_t>(remote->local_irqs());
#else
      info.local_irqs = fidl::VectorPtr<uint16_t>(remote->local_irqs());
#endif
      callback(std::move(info));
    }

   private:
    friend class RprocManager;
    friend class RemoteDevice;
    bool has_ipi_affinity_;
  };

  class RemoteDevice : public RprocDeviceBase {
   public:
    RemoteDevice(HostDevice* host_dev) {
      host_dev->peer_ = this;
      peer_ = host_dev;
    }

    uint16_t ctrl_irq() { return ctrl_irq_; }
    std::vector<uint16_t>& local_irqs() { return local_irqs_; }
    std::vector<uint16_t>& remote_irqs() { return remote_irqs_; }
    bool has_ipi_affinity() { return has_ipi_affinity_; }

    void GetShmVmo(zx::vmo* out_vmo) {
      zx::vmo shm_vmo_duplicated;
      auto status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &shm_vmo_duplicated);
      FXL_CHECK(status == ZX_OK);

      *out_vmo = std::move(shm_vmo_duplicated);
    }

    virtual void GetInfo(GetInfoCallback callback) override {
      RprocDeviceInfo info;

      info.ctrl_irq = ctrl_irq_;
      info.local_irqs = fidl::VectorPtr<uint16_t>(local_irqs_);
#if defined(RPROC_NOTIFY_WITH_PHYS_IRQ)
      info.remote_irqs = fidl::VectorPtr<uint16_t>(remote_irqs_);
#endif
      info.has_ipi_affinity = has_ipi_affinity_;
      info.peer_vmid = peer_vmid_;
      GetShmVmo(&info.shm_vmo);

      callback(std::move(info));
    }

   private:
    friend class RprocManager;

    uint16_t ctrl_irq_;
    std::vector<uint16_t> local_irqs_;
    std::vector<uint16_t> remote_irqs_;
    zx::vmo vmo_;
    bool has_ipi_affinity_;
    std::string resv_mem_;
  };

  class VmStateObserver {
   public:
    VmStateObserver(int16_t vmid,
                    RprocManager* mgr,
                    async_t* async,
                    fidl::InterfaceHandle<VmStateListener> handle)
        : vmid_(vmid), mgr_(mgr), waiter_(this) {
      FXL_CHECK(handle.is_valid());

      waiter_.set_object(handle.channel().get());
      waiter_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
      auto status = waiter_.Begin(async);
      if (status != ZX_OK)
        FXL_LOG(WARNING) << "Failed to wait on channel: status=" << status;

      listener_.Bind(std::move(handle));
    }

    void NotifyVmOnline(uint16_t vmid) { listener_->VmOnline(vmid); }

    void NotifyVmOffline(uint16_t vmid) { listener_->VmOffline(vmid); }

   private:
    async_wait_result_t OnVmDestroyed(async_t* async,
                                      zx_status_t status,
                                      const zx_packet_signal_t* signal) {
      if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        FXL_LOG(INFO) << "VM " << vmid_ << " is terminated";
        async::PostTask(async, [mgr = mgr_, vmid = vmid_] {
          mgr->RemoveVmStateListener(vmid);
        });
      }

      return ASYNC_WAIT_FINISHED;
    }

    int16_t vmid_;
    RprocManager* mgr_;
    VmStateListenerSyncPtr listener_;
    async::WaitMethod<VmStateObserver, &VmStateObserver::OnVmDestroyed> waiter_;
  };

  RprocManager(component::ApplicationContext* application_context);

  static std::unique_ptr<RprocManager> BuildWithDefaultConfig(
      component::ApplicationContext* application_context);

  static std::unique_ptr<RprocManager> BuildFromFile(
      component::ApplicationContext* application_context,
      std::string path);

  static std::unique_ptr<RprocManager> BuildFromString(
      component::ApplicationContext* application_context,
      std::string pbtxt);

  // |RprocService|
  virtual void GetRemoteDevices(int16_t vmid,
                                GetRemoteDevicesCallback callback) override;
  virtual void GetHostDevices(int16_t vmid,
                              GetHostDevicesCallback callback) override;
  virtual void RegisterVmStateListener(
      int16_t vmid,
      fidl::InterfaceHandle<VmStateListener> handle,
      RegisterVmStateListenerCallback callback) override;

  void RemoveVmStateListener(int16_t vmid);

  std::mutex observer_lock_;
  std::unordered_map<int16_t, std::unique_ptr<VmStateObserver>>
      vm_state_observers_;

  void CreateSharedMemoryVmo(std::unique_ptr<RemoteDevice>& remote_dev,
                             SharedMemoryAllocator& allocator) {
    remote_dev->vmo_ = allocator(remote_dev->resv_mem_);
    FXL_CHECK(remote_dev->vmo_.is_valid());

    auto status = remote_dev->vmo_.set_cache_policy(ZX_CACHE_POLICY_CACHED);
    FXL_CHECK(status == ZX_OK);
  }

  void AllocateSharedMemory(SharedMemoryAllocator allocator) {
    for (auto& pair : remote_devs_) {
      for (auto& remote_dev : pair.second) {
        CreateSharedMemoryVmo(remote_dev, allocator);
      }
    }
  }

  void AddBinding(fidl::InterfaceRequest<RprocService> request);

  async_t* async() {
    return loop_.async();
  }

  RemoteDeviceList& sos_remote_devs() {
    return remote_devs_[kSosVmid];
  }
  RemoteDeviceList& remote_devs(int16_t vmid) {
    return remote_devs_[vmid];
  }
  HostDeviceList& host_devs(int16_t vmid) {
    return host_devs_[vmid];
  }

 private:
  static std::unique_ptr<RprocManager> Build(
      component::ApplicationContext* application_context,
      nbl_vmm::RemoteProcConfig& cfg);

  fidl::BindingSet<RprocService> bindings_;
  std::unordered_map<int16_t, RemoteDeviceList> remote_devs_;
  std::unordered_map<int16_t, HostDeviceList> host_devs_;
  async::Loop loop_;
};

}  // namespace machina
