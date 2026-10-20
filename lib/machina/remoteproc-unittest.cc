// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <fuchsia/cpp/machina.h>
#include <lib/app/cpp/application_context.h>
#include <lib/app/cpp/environment_services.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/device/sysinfo.h>

#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/remoteproc_manager.h"
#include "garnet/lib/machina/remoteproc_vm_monitor.h"
#include "garnet/lib/machina/vm_id.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace machina {
namespace {

static void generate_irqs(std::vector<uint16_t>& irqs,
                          uint16_t start,
                          size_t size) {
  irqs.resize(size);
  std::generate(irqs.begin(), irqs.end(), [&start] { return start++; });
}

class RprocManagerTest : public ::testing::Test {
 protected:
  RprocManagerTest() : loop_(&kAsyncLoopConfigMakeDefault) {
    loop_.StartThread();
  }

  async::Loop loop_;
};

TEST_F(RprocManagerTest, Parser) {
  std::string pbtxt = fxl::StringPrintf(R"pbtxt(
    devices {
        remote_vmid: %d
        host_vmid: %d
        ctrl_irq: 100
        local_irqs: {
            start: 101
            length: 10
        }
        remote_irqs: {
            start: 111
            length: 10
        }
    }

    devices {
        remote_vmid: %d
        host_vmid: %d
        ctrl_irq: 130
        local_irqs: { start: 131 }
        local_irqs: { start: 132 }
        local_irqs: { start: 133, length: 5 }
        remote_irqs: { start: 140 }
        remote_irqs: { start: 141 }
        remote_irqs: { start: 142, length: 5 }
    }

    devices {
        remote_vmid: %d
        host_vmid: %d
        ctrl_irq: 170
        local_irqs: { start: 171 }
        remote_irqs: { start: 141 }
    }

    devices {
        remote_vmid: %d
        host_vmid: %d
        ctrl_irq: 200
        local_irqs: {
            start: 201
            length: 5
        }
        local_irqs: {
            start: 206
            length: 5
        }
    }

    devices {
        remote_vmid: %d
        host_vmid: 2
        ctrl_irq: 300
        local_irqs: {
            start: 301
            length: 5
        }
        local_irqs: {
            start: 306
            length: 5
        }
    }
  )pbtxt",
                                         kSosVmid, kAlpsVmid,
                                         kSosVmid, kAlpsVmid,
                                         kSosVmid, kTboxVmid,
                                         kAlpsVmid, kTboxVmid,
                                         kTboxVmid);

  auto mgr = RprocManager::BuildFromString(nullptr, pbtxt);
  ASSERT_TRUE(mgr != nullptr);

  EXPECT_EQ(3U, mgr->sos_remote_devs().size());
  EXPECT_EQ(1U, mgr->remote_devs(kAlpsVmid).size());
  EXPECT_EQ(1U, mgr->remote_devs(kTboxVmid).size());
  EXPECT_EQ(2U, mgr->host_devs(kAlpsVmid).size());
  EXPECT_EQ(2U, mgr->host_devs(kTboxVmid).size());
  EXPECT_EQ(1U, mgr->host_devs(/*vmid=*/2).size());

  std::vector<uint16_t> expected;
  auto& devs = mgr->sos_remote_devs();

  EXPECT_EQ(100, devs[0]->ctrl_irq());
  generate_irqs(expected, 101, 10);
  EXPECT_EQ(expected, devs[0]->local_irqs());
  generate_irqs(expected, 111, 10);
  EXPECT_EQ(expected, devs[0]->remote_irqs());

  EXPECT_EQ(130, devs[1]->ctrl_irq());
  generate_irqs(expected, 131, 7);
  EXPECT_EQ(expected, devs[1]->local_irqs());
  generate_irqs(expected, 140, 7);
  EXPECT_EQ(expected, devs[1]->remote_irqs());

  EXPECT_EQ(170, devs[2]->ctrl_irq());
  generate_irqs(expected, 171, 1);
  EXPECT_EQ(expected, devs[2]->local_irqs());
  generate_irqs(expected, 141, 1);
  EXPECT_EQ(expected, devs[2]->remote_irqs());
}

TEST_F(RprocManagerTest, RprocService) {
  std::string pbtxt = fxl::StringPrintf(R"pbtxt(
    devices {
        remote_vmid: %d
        host_vmid: %d
        ctrl_irq: 100
        local_irqs: {
            start: 101
            length: 10
        }
        remote_irqs: {
            start: 111
            length: 10
        }
    }

    devices {
        remote_vmid: %d
        host_vmid: 2
        ctrl_irq: 130
        local_irqs: { start: 131 }
        remote_irqs: { start: 140 }
    }
  )pbtxt",
                                         kAlpsVmid, kTboxVmid,
                                         kAlpsVmid);

  auto mgr = RprocManager::BuildFromString(nullptr, pbtxt);
  ASSERT_TRUE(mgr != nullptr);

  mgr->AllocateSharedMemory([](std::string& resv_mem) {
    zx::vmo shm_vmo;
    zx::vmo::create(4096, /*option=*/0, &shm_vmo);
    return shm_vmo;
  });

  machina::RprocServiceSyncPtr rproc_svc;
  fidl::Binding<RprocService> binding(mgr.get());
  rproc_svc.Bind(binding.NewBinding(loop_.async()));

  fidl::VectorPtr<fidl::InterfaceHandle<RprocDeviceSvc>> remote_chans;
  rproc_svc->GetRemoteDevices(kAlpsVmid, &remote_chans);

  std::vector<uint16_t> expected;

  RprocDeviceSvcSyncPtr remote0;
  remote0.Bind(std::move((*remote_chans)[0]));

  RprocDeviceInfo info;
  remote0->GetInfo(&info);
  EXPECT_EQ(100, info.ctrl_irq);
  generate_irqs(expected, 101, 10);
  EXPECT_EQ(expected, info.local_irqs.get());
  generate_irqs(expected, 111, 10);
  EXPECT_EQ(expected, info.remote_irqs.get());
  EXPECT_TRUE(info.shm_vmo.is_valid());

  RprocDeviceSvcSyncPtr remote1;
  remote1.Bind(std::move((*remote_chans)[1]));

  remote1->GetInfo(&info);
  EXPECT_EQ(130, info.ctrl_irq);
  generate_irqs(expected, 131, 1);
  EXPECT_EQ(expected, info.local_irqs.get());
  generate_irqs(expected, 140, 1);
  EXPECT_EQ(expected, info.remote_irqs.get());
  EXPECT_TRUE(info.shm_vmo.is_valid());

  fidl::VectorPtr<fidl::InterfaceHandle<RprocDeviceSvc>> host_chans;
  rproc_svc->GetHostDevices(kTboxVmid, &host_chans);
  EXPECT_EQ(1U, (*host_chans).size());

  RprocDeviceSvcSyncPtr host0;
  host0.Bind(std::move((*host_chans)[0]));

  host0->GetInfo(&info);
  EXPECT_EQ(100, info.ctrl_irq);
  generate_irqs(expected, 111, 10);
  EXPECT_EQ(expected, info.local_irqs.get());
  generate_irqs(expected, 101, 10);
  EXPECT_EQ(expected, info.remote_irqs.get());
  EXPECT_TRUE(info.shm_vmo.is_valid());

  rproc_svc->GetHostDevices(/*vmid=*/2, &host_chans);
  EXPECT_EQ(1U, (*host_chans).size());

  RprocDeviceSvcSyncPtr host1;
  host1.Bind(std::move((*host_chans)[0]));

  host1->GetInfo(&info);
  EXPECT_EQ(130, info.ctrl_irq);
  generate_irqs(expected, 140, 1);
  EXPECT_EQ(expected, info.local_irqs.get());
  generate_irqs(expected, 131, 1);
  EXPECT_EQ(expected, info.remote_irqs.get());
  EXPECT_TRUE(info.shm_vmo.is_valid());
}

class RprocDevice : public PeerStateNotifier {
 public:
  RprocDevice(int16_t vmid, RprocDeviceSvcSyncPtr svc) : svc_(std::move(svc)) {
    RprocDeviceInfo info;
    svc_->GetInfo(&info);
    peer_vmid_ = info.peer_vmid;
  }

  int16_t peer_vmid() override { return peer_vmid_; }
  bool is_connected() override { return chan_.is_valid(); }
  void peer_online() override { svc_->GetChannel(&chan_); }
  void peer_offline() override { chan_.reset(); }

 private:
  zx::channel chan_;
  RprocDeviceSvcSyncPtr svc_;
  int16_t peer_vmid_;
};

class FakeGuest {
 public:
  FakeGuest(int16_t vmid, machina::RprocServiceSyncPtr svc, async_t* async)
      : monitor_(vmid, async), rproc_svc_(std::move(svc)) {
    fidl::VectorPtr<fidl::InterfaceHandle<machina::RprocDeviceSvc>> handles;
    rproc_svc_->GetRemoteDevices(vmid, &handles);

    for (auto& handle : *handles) {
      machina::RprocDeviceSvcSyncPtr svc;
      svc.Bind(std::move(handle));

      auto remote_dev =
          std::make_unique<machina::RprocDevice>(vmid, std::move(svc));
      FXL_CHECK(remote_dev != nullptr);

      monitor_.add_notifier(remote_dev.get());
      remote_devs_.push_back(std::move(remote_dev));
    }

    rproc_svc_->GetHostDevices(vmid, &handles);
    for (auto& handle : *handles) {
      machina::RprocDeviceSvcSyncPtr svc;
      svc.Bind(std::move(handle));

      auto host_dev =
          std::make_unique<machina::RprocDevice>(vmid, std::move(svc));
      FXL_CHECK(host_dev != nullptr);

      monitor_.add_notifier(host_dev.get());
      host_devs_.push_back(std::move(host_dev));
    }

    rproc_svc_->RegisterVmStateListener(vmid, monitor_.NewBinding());
  }

  size_t num_remote_devs() { return remote_devs_.size(); }
  size_t num_host_devs() { return host_devs_.size(); }

  RprocDevice* get_remote_dev(size_t id) {
    if (id >= remote_devs_.size())
      return nullptr;
    return remote_devs_[id].get();
  }
  RprocDevice* get_host_dev(size_t id) {
    if (id >= host_devs_.size())
      return nullptr;
    return host_devs_[id].get();
  }

 private:
  using RprocRemoteList = std::vector<std::unique_ptr<machina::RprocDevice>>;
  using RprocHostList = std::vector<std::unique_ptr<machina::RprocDevice>>;

  RprocRemoteList remote_devs_;
  RprocHostList host_devs_;
  RprocVmStateMonitor monitor_;
  machina::RprocServiceSyncPtr rproc_svc_;
};

static zx_status_t create_vm(int16_t vmid,
                             std::unique_ptr<RprocManager>& mgr,
                             async_t* async,
                             std::unique_ptr<FakeGuest>* out_vm) {
  machina::RprocServiceSyncPtr rproc_svc;
  auto request = rproc_svc.NewRequest();
  mgr->AddBinding(std::move(request));

  auto vm = std::make_unique<FakeGuest>(vmid, std::move(rproc_svc), async);
  if (vm == nullptr)
    return ZX_ERR_NO_MEMORY;

  *out_vm = std::move(vm);
  return ZX_OK;
}

static long get_current_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

zx_status_t wait_for(RprocDevice* device, std::function<bool()> callback) {
  auto now = get_current_time_ms();
  while (get_current_time_ms() - now < 2000) {
    if (callback())
      return ZX_OK;
  }

  return ZX_ERR_TIMED_OUT;
}

zx_status_t wait_for_connected(RprocDevice* device) {
  return wait_for(device, [device] { return device->is_connected(); });
}
zx_status_t wait_for_disconnected(RprocDevice* device) {
  return wait_for(device, [device]() { return !device->is_connected(); });
}

class AutoConnectTest : public ::testing::Test {
 protected:
  AutoConnectTest() : loop_(&kAsyncLoopConfigMakeDefault) {
    loop_.StartThread();
  }

  void SetUp() override {
    std::string pbtxt = fxl::StringPrintf(R"pbtxt(
      devices {
          remote_vmid: %d
          host_vmid: %d
      }
      devices {
          remote_vmid: %d
          host_vmid: %d
      }
      devices {
          remote_vmid: %d
          host_vmid: %d
      }
    )pbtxt",
                                           kSosVmid, kAlpsVmid,
                                           kSosVmid, kTboxVmid,
                                           kAlpsVmid, kTboxVmid);

    mgr_ = RprocManager::BuildFromString(nullptr, pbtxt);
    ASSERT_TRUE(mgr_ != nullptr);

    mgr_->AllocateSharedMemory([](std::string& resv_mem) {
      zx::vmo shm_vmo;
      zx::vmo::create(4096, /*option=*/0, &shm_vmo);
      return shm_vmo;
    });

    auto status = create_vm(kSosVmid, mgr_, loop_.async(), &sos_);
    ASSERT_EQ(ZX_OK, status);

    EXPECT_EQ(0U, sos_->num_host_devs());
    EXPECT_EQ(2U, sos_->num_remote_devs());

    sos_remote0_ = sos_->get_remote_dev(/*idx=*/0);
    ASSERT_TRUE(sos_remote0_ != nullptr);
    EXPECT_TRUE(!sos_remote0_->is_connected());
    sos_remote1_ = sos_->get_remote_dev(/*idx=*/1);
    ASSERT_TRUE(sos_remote1_ != nullptr);
    EXPECT_TRUE(!sos_remote1_->is_connected());
  }

  async::Loop loop_;
  std::unique_ptr<RprocManager> mgr_;
  std::unique_ptr<FakeGuest> sos_;
  RprocDevice* sos_remote0_;
  RprocDevice* sos_remote1_;
};

TEST_F(AutoConnectTest, DualHostVm) {
  std::unique_ptr<FakeGuest> uos0, uos1;
  auto status = create_vm(0, mgr_, loop_.async(), &uos0);
  ASSERT_EQ(ZX_OK, status);

  auto uos0_host = uos0->get_host_dev(/*idx=*/0);

  EXPECT_EQ(ZX_OK, wait_for_connected(sos_remote0_));
  EXPECT_EQ(ZX_OK, wait_for_connected(uos0_host));
  uos0.reset();
  EXPECT_EQ(ZX_OK, wait_for_disconnected(sos_remote0_));
  EXPECT_EQ(ZX_OK, wait_for_disconnected(uos0_host));

  status = create_vm(1, mgr_, loop_.async(), &uos1);
  ASSERT_EQ(ZX_OK, status);

  auto uos1_host = uos1->get_host_dev(/*idx=*/0);

  EXPECT_EQ(ZX_OK, wait_for_connected(sos_remote1_));
  EXPECT_EQ(ZX_OK, wait_for_connected(uos1_host));
  uos1.reset();
  EXPECT_EQ(ZX_OK, wait_for_disconnected(sos_remote1_));
  EXPECT_EQ(ZX_OK, wait_for_disconnected(uos1_host));
}

TEST_F(AutoConnectTest, RemoteVmBootFirst) {
  std::unique_ptr<FakeGuest> uos0, uos1;
  auto status = create_vm(0, mgr_, loop_.async(), &uos0);
  ASSERT_EQ(ZX_OK, status);

  auto uos0_remote = uos0->get_remote_dev(/*idx=*/0);
  ASSERT_TRUE(uos0_remote != nullptr);
  EXPECT_TRUE(!uos0_remote->is_connected());

  status = create_vm(1, mgr_, loop_.async(), &uos1);
  ASSERT_EQ(ZX_OK, status);

  auto uos1_host = uos1->get_host_dev(/*idx=*/1);
  ASSERT_TRUE(uos1_host != nullptr);

  EXPECT_EQ(ZX_OK, wait_for_connected(uos0_remote));
  EXPECT_EQ(ZX_OK, wait_for_connected(uos1_host));
}

TEST_F(AutoConnectTest, RemoteVmDestroyed) {
  std::unique_ptr<FakeGuest> uos0, uos1;
  auto status = create_vm(0, mgr_, loop_.async(), &uos0);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(1U, uos0->num_remote_devs());

  status = create_vm(1, mgr_, loop_.async(), &uos1);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(2U, uos1->num_host_devs());

  auto uos0_remote = uos0->get_remote_dev(/*idx=*/0);
  ASSERT_TRUE(uos0_remote != nullptr);
  EXPECT_EQ(ZX_OK, wait_for_connected(uos0_remote));

  auto uos1_host = uos1->get_host_dev(/*idx=*/1);
  ASSERT_TRUE(uos1_host != nullptr);
  EXPECT_EQ(ZX_OK, wait_for_connected(uos1_host));

  uos0.reset();
  EXPECT_EQ(ZX_OK, wait_for_disconnected(uos1_host));
  EXPECT_EQ(ZX_OK, wait_for_disconnected(uos0_remote));
}

TEST_F(AutoConnectTest, HostVmBootFirst) {
  std::unique_ptr<FakeGuest> uos0, uos1;
  auto status = create_vm(1, mgr_, loop_.async(), &uos1);
  ASSERT_EQ(ZX_OK, status);

  auto uos1_host = uos1->get_host_dev(/*idx=*/1);
  ASSERT_TRUE(uos1_host != nullptr);
  EXPECT_TRUE(!uos1_host->is_connected());

  status = create_vm(0, mgr_, loop_.async(), &uos0);
  ASSERT_EQ(ZX_OK, status);

  auto uos0_remote = uos0->get_remote_dev(/*idx=*/0);
  ASSERT_TRUE(uos1_host != nullptr);

  EXPECT_EQ(ZX_OK, wait_for_connected(uos0_remote));
  EXPECT_EQ(ZX_OK, wait_for_connected(uos1_host));
}

TEST_F(AutoConnectTest, HostVmDestroyed) {
  std::unique_ptr<FakeGuest> uos0, uos1;
  auto status = create_vm(0, mgr_, loop_.async(), &uos0);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(1U, uos0->num_remote_devs());

  status = create_vm(1, mgr_, loop_.async(), &uos1);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(2U, uos1->num_host_devs());

  auto uos0_remote = uos0->get_remote_dev(/*idx=*/0);
  ASSERT_TRUE(uos0_remote != nullptr);
  EXPECT_EQ(ZX_OK, wait_for_connected(uos0_remote));

  auto uos1_host = uos1->get_host_dev(/*idx=*/1);
  ASSERT_TRUE(uos1_host != nullptr);
  EXPECT_EQ(ZX_OK, wait_for_connected(uos1_host));

  uos1.reset();
  EXPECT_EQ(ZX_OK, wait_for_disconnected(uos1_host));
  EXPECT_EQ(ZX_OK, wait_for_disconnected(uos0_remote));
}

}  // namespace

}  // namespace machina
