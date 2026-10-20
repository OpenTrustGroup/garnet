// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2020 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <fuchsia/cpp/machina.h>
#include <lib/app/cpp/application_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/zx/vmar.h>
#include <zircon/device/sysinfo.h>

#include <fcntl.h>

#include <memory>
#include <thread>
#include <vector>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/vhm_device.h"

#include "garnet/bin/guest/proto/vm_config.pb.h"

namespace machina {
namespace {

static constexpr uintptr_t kAddressWithoutMapping = 0xFFFFFFF0;
static constexpr uint8_t kNumVcpu = 4;
static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

enum FakeMmioDevice {
  ScratchPad = 0x1000,
  WriteCounter = 0x1008,
  ResetWriteCounter = 0x1010,
  Invalid = 0x1018,
};

enum FakeDmaDevice {
  SourceAddr = 0x2000,
  DestAddr = 0x2008,
  BytesToCopy = 0x2010,
  Trigger = 0x2018,
  IrqLine = 0x2020,
};

enum AsyncEventDevice {
  TriggerEvent = 0x3000,
  WaitForEvent = 0x3008,
  EventSignaled = 0x3010,
  TriggerIrq = 0x3018,
  GetIrqNumber = 0x3020,
};

static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

class FakeVhmDevice : public VhmDevice {
 public:
  FakeVhmDevice()
      : VhmDevice(/*vmid=*/0),
        context_(component::ApplicationContext::CreateFromStartupInfo()) {
    context_->ConnectToEnvironmentService(machina::VhmService::Name_,
                                          NewRequest().TakeChannel());
    FetchVmConfig();
    InitInternal();

    zx_status_t status = zx::event::create(0, &irq_event_);
    FXL_CHECK(status == ZX_OK);
  }

  void OnInterrupt(uint32_t irq) override {
    irq_triggered_ = irq;
    irq_event_.signal(0, ZX_USER_SIGNAL_0);
  }

  zx_status_t WaitForInterrupt() {
    zx_signals_t observed;
    return irq_event_.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::sec(1)),
                               &observed);
  }

  uint32_t irq_triggered() { return irq_triggered_; }

  template <typename T>
  zx_status_t Read(uint8_t vcpu_id, uintptr_t addr, T* value) const {
    IoValue io_value = {sizeof(T), {.u64 = 0}};
    auto ret = ReadInternal(vcpu_id, addr, &io_value);
    memcpy(value, io_value.data, sizeof(T));
    return ret;
  }

  template <typename T>
  zx_status_t Write(uint8_t vcpu_id, uintptr_t addr, const T& value) {
    IoValue io_value;
    io_value.access_size = sizeof(T);
    memcpy(io_value.data, &value, sizeof(T));
    return WriteInternal(vcpu_id, addr, io_value);
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;

  zx::event irq_event_;
  uint32_t irq_triggered_ = 0;
};

class VhmDeviceTest : public ::testing::Test {
 public:
  VhmDeviceTest() : loop_(&kAsyncLoopConfigMakeDefault) {
    zx::resource rsc;
    zx_status_t status = get_root_resource(&rsc);
    FXL_CHECK(status == ZX_OK);

    nbl_vmm::VmConfig cfg;
    FXL_CHECK(cfg.ParseFromString(device_.cfg()));

    zx_handle_t vmo_handle;
    auto vmem = cfg.mem(0);
    status = zx_vmo_create_physical(rsc.get(), vmem.hpa_base(), vmem.size(),
                                    &vmo_handle);
    FXL_CHECK(status == ZX_OK);

    zx::vmo vmo(vmo_handle);
    status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
    FXL_CHECK(status == ZX_OK);

    status = phys_mem_.Init(std::move(vmo), vmem.hpa_base());
    FXL_CHECK(status == ZX_OK);
  }

 protected:
  virtual void SetUp() override { loop_.StartThread(); }

  virtual void TearDown() override {
    loop_.Quit();
    loop_.JoinThreads();
  }

  async::Loop loop_;
  FakeVhmDevice device_;
  PhysMem phys_mem_;
};

TEST_F(VhmDeviceTest, InvalidAddress) {
  uint8_t vcpu_id = 0;

  uint64_t dummy;
  EXPECT_EQ(device_.Write(vcpu_id, FakeMmioDevice::Invalid, dummy),
            ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::Invalid, &dummy),
            ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(device_.Write(vcpu_id, kAddressWithoutMapping, dummy),
            ZX_ERR_NOT_FOUND);

  EXPECT_EQ(device_.Read(vcpu_id, kAddressWithoutMapping, &dummy),
            ZX_ERR_NOT_FOUND);
}

TEST_F(VhmDeviceTest, SyncMmioRead) {
  uint8_t vcpu_id = 0;

  uint64_t to_write = 0xdeadbeefdeadbeef;
  EXPECT_EQ(device_.Write(vcpu_id, FakeMmioDevice::ScratchPad, to_write),
            ZX_OK);

  uint8_t u8;
  EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u8), ZX_OK);
  EXPECT_EQ(u8, 0xef);

  uint16_t u16;
  EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u16), ZX_OK);
  EXPECT_EQ(u16, 0xbeef);

  uint32_t u32;
  EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u32), ZX_OK);
  EXPECT_EQ(u32, 0xdeadbeef);

  uint64_t u64;
  EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u64), ZX_OK);
  EXPECT_EQ(u64, 0xdeadbeefdeadbeef);
}

TEST_F(VhmDeviceTest, MultiThreadedSyncMmioRead) {
  uint64_t to_write = 0xdeadbeefdeadbeef;
  EXPECT_EQ(device_.Write(/*vcpu_id=*/0, FakeMmioDevice::ScratchPad, to_write),
            ZX_OK);

  std::vector<std::unique_ptr<std::thread>> vcpu_threads(kNumVcpu);
  for (uint8_t vcpu_id = 0; vcpu_id < kNumVcpu; vcpu_id++) {
    vcpu_threads[vcpu_id].reset(new std::thread([this, vcpu_id] {
      uint8_t u8;
      EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u8), ZX_OK);
      EXPECT_EQ(u8, 0xef);

      uint16_t u16;
      EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u16), ZX_OK);
      EXPECT_EQ(u16, 0xbeef);

      uint32_t u32;
      EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u32), ZX_OK);
      EXPECT_EQ(u32, 0xdeadbeef);

      uint64_t u64;
      EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::ScratchPad, &u64), ZX_OK);
      EXPECT_EQ(u64, 0xdeadbeefdeadbeef);
    }));
  }

  for (auto& thread : vcpu_threads) {
    thread->join();
  }
}

TEST_F(VhmDeviceTest, SyncMmioWrite) {
  uint8_t vcpu_id = 0;
  uint8_t dummy;
  EXPECT_EQ(device_.Write(vcpu_id, FakeMmioDevice::ResetWriteCounter, dummy),
            ZX_OK);
  EXPECT_EQ(device_.Write(vcpu_id, FakeMmioDevice::WriteCounter, dummy), ZX_OK);

  uint32_t counter;
  EXPECT_EQ(device_.Read(vcpu_id, FakeMmioDevice::WriteCounter, &counter),
            ZX_OK);
  EXPECT_EQ(1u, counter);
}

TEST_F(VhmDeviceTest, MultiThreadedSyncMmioWrite) {
  uint8_t dummy;
  EXPECT_EQ(
      device_.Write(/*vcpu_id=*/0, FakeMmioDevice::ResetWriteCounter, dummy),
      ZX_OK);

  std::vector<std::unique_ptr<std::thread>> vcpu_threads(kNumVcpu);
  for (uint8_t vcpu_id = 0; vcpu_id < kNumVcpu; vcpu_id++) {
    vcpu_threads[vcpu_id].reset(new std::thread([this, vcpu_id, &dummy] {
      EXPECT_EQ(device_.Write(vcpu_id, FakeMmioDevice::WriteCounter, dummy),
                ZX_OK);
    }));
  }

  for (auto& thread : vcpu_threads) {
    thread->join();
  }

  uint32_t counter;
  EXPECT_EQ(device_.Read(/*vcpu_id=*/0, FakeMmioDevice::WriteCounter, &counter),
            ZX_OK);
  EXPECT_EQ(kNumVcpu, counter);
}

static void generate_test_pattern(uint8_t* base, size_t size) {
  for (uint32_t i = 0; i < size; i++) {
    *(base + i) = i;
  }
}

TEST_F(VhmDeviceTest, IrqInjection) {
  uintptr_t guest_src_paddr = 0x100000;
  uintptr_t guest_dst_paddr = 0x200000;
  size_t bytes_to_copy = 0x1000;
  uint32_t irqline = 33;

  auto src_ptr = phys_mem_.as<uint8_t>(guest_src_paddr);
  generate_test_pattern(src_ptr, bytes_to_copy);

  EXPECT_EQ(
      device_.Write(/*vcpu_id=*/0, FakeDmaDevice::SourceAddr, guest_src_paddr),
      ZX_OK);
  EXPECT_EQ(
      device_.Write(/*vcpu_id=*/0, FakeDmaDevice::DestAddr, guest_dst_paddr),
      ZX_OK);
  EXPECT_EQ(
      device_.Write(/*vcpu_id=*/0, FakeDmaDevice::BytesToCopy, bytes_to_copy),
      ZX_OK);
  EXPECT_EQ(device_.Write(/*vcpu_id=*/0, FakeDmaDevice::IrqLine, irqline),
            ZX_OK);

  uint8_t dummy;
  EXPECT_EQ(device_.Write(/*vcpu_id=*/0, FakeDmaDevice::Trigger, dummy), ZX_OK);

  auto status = device_.WaitForInterrupt();
  EXPECT_EQ(ZX_OK, status);

  EXPECT_EQ(irqline, device_.irq_triggered());

  auto dst_ptr = phys_mem_.as<uint8_t>(guest_dst_paddr);
  EXPECT_TRUE(memcmp(src_ptr, dst_ptr, bytes_to_copy) == 0);
}

TEST_F(VhmDeviceTest, IoEventFd) {
  EXPECT_EQ(device_.Write(/*vcpu_id=*/0, AsyncEventDevice::TriggerEvent, 0),
            ZX_OK);
  EXPECT_EQ(device_.Write(/*vcpu_id=*/0, AsyncEventDevice::WaitForEvent, 0),
            ZX_OK);

  bool signaled;
  EXPECT_EQ(
      device_.Read(/*vcpu_id=*/0, AsyncEventDevice::EventSignaled, &signaled),
      ZX_OK);
  EXPECT_TRUE(signaled);
}

TEST_F(VhmDeviceTest, IrqFd) {
  uint32_t expected_irq;
  EXPECT_EQ(device_.Read(/*vcpu_id=*/0, AsyncEventDevice::GetIrqNumber,
                         &expected_irq),
            ZX_OK);
  EXPECT_EQ(device_.Write(/*vcpu_id=*/0, AsyncEventDevice::TriggerIrq, 0),
            ZX_OK);

  auto status = device_.WaitForInterrupt();
  EXPECT_EQ(ZX_OK, status);

  EXPECT_EQ(expected_irq, device_.irq_triggered());
}

}  // namespace
}  // namespace machina