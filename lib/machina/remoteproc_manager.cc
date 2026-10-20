// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoteproc_manager.h"

#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/fxl/strings/string_printf.h"

using google::protobuf::RepeatedPtrField;
namespace machina {

// Default rproc config for mt8678
static std::string BuildDefaultRprocConfig() {
  return fxl::StringPrintf(R"pbtxt(
  devices {
      remote_vmid: %d
      host_vmid: %d
      ctrl_irq: 200
      local_irqs: {
          start: 201
          length: 40
      }
      remote_irqs: {
          start: 241
          length: 40
      }
      resv_mem: "rproc_reserved"
      host_ipi_affinity: true
      remote_ipi_affinity: true
  }
)pbtxt",
                           kSosVmid, kAlpsVmid);
}

std::unique_ptr<RprocManager> RprocManager::BuildFromString(
    component::ApplicationContext* application_context,
    std::string pbtxt) {
  nbl_vmm::RemoteProcConfig cfg;
  auto success = google::protobuf::TextFormat::ParseFromString(pbtxt, &cfg);
  FXL_CHECK(success);

  return Build(application_context, cfg);
}

std::unique_ptr<RprocManager> RprocManager::BuildFromFile(
    component::ApplicationContext* application_context,
    std::string path) {
  auto fd = open(path.c_str(), O_RDONLY);
  FXL_CHECK(fd > 0);

  google::protobuf::io::FileInputStream fin(fd);
  fin.SetCloseOnDelete(true);
  nbl_vmm::RemoteProcConfig cfg;
  auto success = google::protobuf::TextFormat::Parse(&fin, &cfg);
  FXL_CHECK(success);

  return Build(application_context, cfg);
}

std::unique_ptr<RprocManager> RprocManager::BuildWithDefaultConfig(
    component::ApplicationContext* application_context) {
  return BuildFromString(application_context, BuildDefaultRprocConfig());
}

static void ParseIrqRange(
    std::vector<uint16_t>& out_irqs,
    const RepeatedPtrField<nbl_vmm::IrqRange>& irq_ranges) {
  for (auto& irq_range : irq_ranges) {
    uint16_t start = irq_range.start();
    size_t size = irq_range.length();
    if (size == 0)
      size = 1;

    std::vector<uint16_t> irqs;
    irqs.resize(size);
    std::generate(irqs.begin(), irqs.end(), [&start] { return start++; });
    out_irqs.insert(out_irqs.end(), irqs.begin(), irqs.end());
  }
}

std::unique_ptr<RprocManager> RprocManager::Build(
    component::ApplicationContext* application_context,
    nbl_vmm::RemoteProcConfig& cfg) {
  auto mgr = std::make_unique<RprocManager>(application_context);
  FXL_CHECK(mgr != nullptr);

  for (int i = 0; i < cfg.devices_size(); i++) {
    auto rproc_dev = cfg.devices(i);

    std::unique_ptr<RprocRemote> rproc;
    auto host_vmid = rproc_dev.host_vmid();
    auto remote_vmid = rproc_dev.remote_vmid();

    auto host_dev = std::make_unique<HostDevice>();
    FXL_CHECK(host_dev != nullptr);

    host_dev->has_ipi_affinity_ = rproc_dev.host_ipi_affinity();
    host_dev->vmid_ = host_vmid;
    host_dev->peer_vmid_ = remote_vmid;
    host_dev->online_ = false;
    host_dev->connected_ = false;

    auto remote_dev = std::make_unique<RemoteDevice>(host_dev.get());
    FXL_CHECK(remote_dev != nullptr);

    remote_dev->ctrl_irq_ = rproc_dev.ctrl_irq();
    remote_dev->resv_mem_ = rproc_dev.resv_mem();
    remote_dev->has_ipi_affinity_ = rproc_dev.remote_ipi_affinity();
    remote_dev->vmid_ = remote_vmid;
    remote_dev->peer_vmid_ = host_vmid;
    remote_dev->online_ = false;
    remote_dev->connected_ = false;
    ParseIrqRange(remote_dev->local_irqs_, rproc_dev.local_irqs());
    ParseIrqRange(remote_dev->remote_irqs_, rproc_dev.remote_irqs());

    mgr->remote_devs_[remote_vmid].push_back(std::move(remote_dev));
    mgr->host_devs_[host_vmid].push_back(std::move(host_dev));
  }

  return mgr;
}

RprocManager::RprocManager(component::ApplicationContext* application_context) {
  FXL_CHECK(loop_.StartThread() == ZX_OK);

  if (application_context)
    application_context->outgoing_services()->AddService<RprocService>(
        [this](fidl::InterfaceRequest<RprocService> request) {
          bindings_.AddBinding(this, std::move(request));
        });
}

void RprocManager::AddBinding(fidl::InterfaceRequest<RprocService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void RprocManager::GetRemoteDevices(int16_t vmid,
                                    GetRemoteDevicesCallback callback) {
  fidl::VectorPtr<fidl::InterfaceHandle<RprocDeviceSvc>> dev_svcs;

  for (auto& remote_dev : remote_devs(vmid)) {
    auto handle = remote_dev->NewBinding(loop_.async());
    dev_svcs.push_back(std::move(handle));
  }

  callback(std::move(dev_svcs));
}

void RprocManager::GetHostDevices(int16_t vmid,
                                  GetHostDevicesCallback callback) {
  fidl::VectorPtr<fidl::InterfaceHandle<RprocDeviceSvc>> dev_svcs;

  for (auto& host_dev : host_devs(vmid)) {
    auto handle = host_dev->NewBinding(loop_.async());
    dev_svcs.push_back(std::move(handle));
  }

  callback(std::move(dev_svcs));
}

void RprocManager::RegisterVmStateListener(
    int16_t vmid,
    fidl::InterfaceHandle<VmStateListener> handle,
    RegisterVmStateListenerCallback callback) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  for (auto& it : remote_devs_) {
    for (auto& dev : it.second) {
      dev->NotifyVmOnline(vmid);
    }
  }
  for (auto& it : host_devs_) {
    for (auto& dev : it.second)
      dev->NotifyVmOnline(vmid);
  }
  for (auto& it : vm_state_observers_)
    it.second->NotifyVmOnline(vmid);

  auto observer =
      std::make_unique<VmStateObserver>(vmid, this, async(), std::move(handle));
  FXL_CHECK(observer != nullptr);

  for (auto& it : vm_state_observers_)
    observer->NotifyVmOnline(it.first);

  FXL_LOG(INFO) << "registered observer, vmid=" << vmid;
  vm_state_observers_[vmid] = std::move(observer);

  callback();
}

void RprocManager::RemoveVmStateListener(int16_t vmid) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  FXL_LOG(INFO) << "removed observer, vmid=" << vmid;
  vm_state_observers_.erase(vmid);

  for (auto& it : remote_devs_) {
    for (auto& dev : it.second)
      dev->NotifyVmOffline(vmid);
  }
  for (auto& it : host_devs_) {
    for (auto& dev : it.second)
      dev->NotifyVmOffline(vmid);
  }
  for (auto& it : vm_state_observers_)
    it.second->NotifyVmOffline(vmid);
}

}  // namespace machina
