// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc_service.h>
#include <zx/channel.h>
#include <zx/resource.h>

#include "garnet/lib/gzos/trusty_virtio/remote_system_fake.h"
#include "garnet/lib/gzos/trusty_virtio/trusty_virtio_device.h"
#include "gtest/gtest.h"

namespace trusty_virtio {

const trusty_vdev_descr kVdevDescriptors[] = {
    DECLARE_TRUSTY_VIRTIO_DEVICE_DESCR(kTipcDeviceId, "dev0", 12, 16),
    DECLARE_TRUSTY_VIRTIO_DEVICE_DESCR(kTipcDeviceId, "dev1", 20, 24),
    DECLARE_TRUSTY_VIRTIO_DEVICE_DESCR(kTipcDeviceId, "dev2", 28, 32),
};

class ResourceTableTest : public ::testing::Test {
 public:
  ResourceTableTest() : loop_(&kAsyncLoopConfigAttachToThread) {}

 protected:
  virtual void SetUp() {
    zx::resource shm_rsc;
    ASSERT_EQ(get_shm_resource(&shm_rsc), ZX_OK);
	
    // Create Shared Memory
    ASSERT_EQ(SharedMem::Create(shm_rsc, &shared_mem_), ZX_OK);

    // Create VirtioBus
    fbl::AllocChecker ac;
    bus_ = fbl::make_unique_checked<VirtioBus>(&ac, shared_mem_);
    ASSERT_TRUE(ac.check());

    // Create fake remote system
    remote_ = fbl::move(RemoteSystemFake::Create(shared_mem_));
    ASSERT_TRUE(remote_ != nullptr);

    // Create some devices on the bus
    for (uint32_t i = 0; i < fbl::count_of(kVdevDescriptors); i++) {
      zx::channel h0, h1;
      ASSERT_EQ(zx::channel::create(0, &h0, &h1), ZX_OK);
      fbl::RefPtr<TrustyVirtioDevice> dev =
          fbl::AdoptRef(new TrustyVirtioDevice(
              kVdevDescriptors[i], loop_.dispatcher(), fbl::move(h1)));
      ASSERT_TRUE(dev != nullptr);

      ASSERT_EQ(bus_->AddDevice(dev), ZX_OK);
    }
  }

  size_t ResourceTableSize(void) {
    size_t num_devices = fbl::count_of(kVdevDescriptors);
    return sizeof(resource_table) +
           (sizeof(uint32_t) + sizeof(trusty_vdev_descr)) * num_devices;
  }

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::unique_ptr<VirtioBus> bus_;
  fbl::unique_ptr<RemoteSystemFake> remote_;
  async::Loop loop_;

 private:
  static constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";

  zx_status_t get_shm_resource(zx::resource* resource) {
    fbl::unique_fd fd(open(kSysInfoPath, O_RDWR));
    if (!fd) {
      return ZX_ERR_IO;
    }
    ssize_t n = ioctl_sysinfo_get_ns_shm_resource(
        fd.get(), resource->reset_and_get_address());
    return n < 0 ? ZX_ERR_IO : ZX_OK;
  }
};

TEST_F(ResourceTableTest, GetResourceTable) {
  size_t buf_size = ResourceTableSize();
  auto buf = remote_->AllocBuffer(buf_size);
  ASSERT_TRUE(buf != nullptr);
  ASSERT_EQ(bus_->GetResourceTable(buf, &buf_size), ZX_OK);

  resource_table* table = reinterpret_cast<resource_table*>(buf);
  EXPECT_EQ(table->ver, kVirtioResourceTableVersion);
  EXPECT_EQ(table->num, fbl::count_of(kVdevDescriptors));
  for (uint32_t i = 0; i < table->num; i++) {
    auto expected_desc = &kVdevDescriptors[i];
    auto expected_tx_num = expected_desc->vrings[kTxQueue].num;
    auto expected_rx_num = expected_desc->vrings[kRxQueue].num;
    auto expected_dev_name = expected_desc->config.dev_name;

    auto descr = rsc_entry<trusty_vdev_descr>(table, i);
    EXPECT_EQ(descr->hdr.type, RSC_VDEV);
    EXPECT_EQ(descr->vdev.config_len, sizeof(trusty_vdev_config));
    EXPECT_EQ(descr->vdev.num_of_vrings, kNumQueues);
    EXPECT_EQ(descr->vdev.id, kTipcDeviceId);
    EXPECT_EQ(descr->vdev.notifyid, bus_->devices()[i]->notify_id());
    EXPECT_EQ(descr->vrings[kTxQueue].align, (uint32_t)PAGE_SIZE);
    EXPECT_EQ(descr->vrings[kTxQueue].num, expected_tx_num);
    EXPECT_EQ(descr->vrings[kTxQueue].notifyid, kTxQueue);
    EXPECT_EQ(descr->vrings[kRxQueue].align, (uint32_t)PAGE_SIZE);
    EXPECT_EQ(descr->vrings[kRxQueue].num, expected_rx_num);
    EXPECT_EQ(descr->vrings[kRxQueue].notifyid, kRxQueue);
    EXPECT_EQ(descr->config.msg_buf_max_size, (uint32_t)PAGE_SIZE);
    EXPECT_EQ(descr->config.msg_buf_alignment, (uint32_t)PAGE_SIZE);
    EXPECT_STREQ(descr->config.dev_name, expected_dev_name);
  }
}

TEST_F(ResourceTableTest, FailedToGetResourceTable) {
  auto buf = shared_mem_->as<uint8_t>(0);

  // pass smaller buffer not able to fit resource table should fail
  size_t buf_size = ResourceTableSize() - 1;
  ASSERT_EQ(bus_->GetResourceTable(buf, &buf_size), ZX_ERR_NO_MEMORY);

  // pass buffer outside the shared memory region should fail
  buf_size = shared_mem_->size();
  ASSERT_EQ(bus_->GetResourceTable(buf - 1, &buf_size), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(bus_->GetResourceTable(buf + buf_size - 1, &buf_size),
            ZX_ERR_OUT_OF_RANGE);
}

class VirtioBusStateTest : public ::testing::Test {
 public:
  VirtioBusStateTest() : loop_(&kAsyncLoopConfigAttachToThread) {}

 protected:
  virtual void SetUp() {
    // Create Shared Memory
    ASSERT_EQ(SharedMem::Create(&shared_mem_), ZX_OK);

    // Create VirtioBus
    bus_ = fbl::make_unique<VirtioBus>(shared_mem_);
    ASSERT_TRUE(bus_ != nullptr);

    // Create fake remote system
    remote_ = fbl::move(RemoteSystemFake::Create(shared_mem_));
    ASSERT_TRUE(remote_ != nullptr);

    // Create device on the bus
    zx::channel connector;
    ASSERT_EQ(zx::channel::create(0, &channel_, &connector), ZX_OK);
    trusty_vdev_ = fbl::AdoptRef(new TrustyVirtioDevice(
        kVdevDescriptors[0], loop_.dispatcher(), fbl::move(connector)));
    ASSERT_TRUE(trusty_vdev_ != nullptr);
    ASSERT_EQ(bus_->AddDevice(trusty_vdev_), ZX_OK);

    rsc_table_size_ = PAGE_SIZE;
    rsc_table_ = remote_->AllocBuffer(rsc_table_size_);
    ASSERT_TRUE(rsc_table_ != nullptr);
    ASSERT_EQ(bus_->GetResourceTable(rsc_table_, &rsc_table_size_), ZX_OK);

    auto table = reinterpret_cast<resource_table*>(rsc_table_);
    ASSERT_EQ(remote_->HandleResourceTable(table, bus_->devices()), ZX_OK);
  }

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::RefPtr<TrustyVirtioDevice> trusty_vdev_;
  fbl::unique_ptr<VirtioBus> bus_;
  async::Loop loop_;
  zx::channel channel_;
  fbl::unique_ptr<RemoteSystemFake> remote_;

  void* rsc_table_;
  size_t rsc_table_size_;
};

TEST_F(VirtioBusStateTest, StartStopTest) {
  ASSERT_EQ(bus_->Start(rsc_table_, rsc_table_size_), ZX_OK);
  // Start again should get error
  ASSERT_EQ(bus_->Start(rsc_table_, rsc_table_size_), ZX_ERR_BAD_STATE);

  ASSERT_EQ(bus_->Stop(rsc_table_, rsc_table_size_), ZX_OK);
  // Stop again should get error
  ASSERT_EQ(bus_->Stop(rsc_table_, rsc_table_size_), ZX_ERR_BAD_STATE);
}

TEST_F(VirtioBusStateTest, ResetDeviceTest) {
  // Reset device before bus start
  ASSERT_EQ(bus_->ResetDevice(trusty_vdev_->notify_id()), ZX_ERR_BAD_STATE);

  ASSERT_EQ(bus_->Start(rsc_table_, rsc_table_size_), ZX_OK);
  ASSERT_EQ(bus_->ResetDevice(trusty_vdev_->notify_id()), ZX_OK);
  // Reset device again should not error
  ASSERT_EQ(bus_->ResetDevice(trusty_vdev_->notify_id()), ZX_OK);

  // Reset device with invalid device id
  ASSERT_EQ(bus_->ResetDevice(trusty_vdev_->notify_id() + 1), ZX_ERR_NOT_FOUND);
}

TEST_F(VirtioBusStateTest, KickVqueueTest) {
  // Kick vqueue before bus start
  ASSERT_EQ(bus_->KickVqueue(trusty_vdev_->notify_id(), 0), ZX_ERR_BAD_STATE);

  ASSERT_EQ(bus_->Start(rsc_table_, rsc_table_size_), ZX_OK);
  ASSERT_EQ(bus_->KickVqueue(trusty_vdev_->notify_id(), 0), ZX_OK);
  ASSERT_EQ(bus_->KickVqueue(trusty_vdev_->notify_id(), 1), ZX_OK);

  // Kick vqueue with invalid device id
  ASSERT_EQ(bus_->KickVqueue(trusty_vdev_->notify_id() + 1, 0),
            ZX_ERR_NOT_FOUND);

  // Kick vqueue with invalid vqueue id
  ASSERT_EQ(bus_->KickVqueue(trusty_vdev_->notify_id(), kNumQueues),
            ZX_ERR_NOT_FOUND);
}

class TransactionTest : public VirtioBusStateTest {
 protected:
  static constexpr size_t kMsgBufferSize = 256;

  virtual void SetUp() {
    VirtioBusStateTest::SetUp();
    VirtioBusStart();
  }

  void VirtioBusStart() {
    auto frontend = remote_->GetFrontend(trusty_vdev_->notify_id());
    ASSERT_TRUE(frontend != nullptr);

    // Sent the processed resource table to VirtioBus, and notify
    // it to start service.
    ASSERT_EQ(bus_->Start(rsc_table_, rsc_table_size_), ZX_OK);

    loop_.RunUntilIdle();
  }
};

TEST_F(TransactionTest, ReceiveTest) {
  constexpr int kNumRxBuffers = 2;

  const auto frontend = remote_->GetFrontend(trusty_vdev_->notify_id());
  ASSERT_TRUE(frontend != nullptr);

  // allocate some RX buffers
  for (int i = 0; i < kNumRxBuffers; i++) {
    size_t msg_size = PAGE_SIZE;
    auto msg_buf = remote_->AllocBuffer(msg_size);
    ASSERT_TRUE(msg_buf != nullptr);

    ASSERT_EQ(frontend->rx_queue()
                  .BuildDescriptor()
                  .AppendWriteable(msg_buf, msg_size)
                  .Build(),
              ZX_OK);
  }

  // Write some messages to the trusty virtio device
  const char msg1[] = "This is the first message";
  const char msg2[] = "This is the second message";
  ASSERT_EQ(channel_.write(0, msg1, sizeof(msg1), NULL, 0), ZX_OK);
  ASSERT_EQ(channel_.write(0, msg2, sizeof(msg2), NULL, 0), ZX_OK);

  loop_.RunUntilIdle();

  // Verify the message from remote side
  virtio_desc_t desc;
  volatile vring_used_elem* used = frontend->rx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(frontend->rx_queue().queue()->ReadDesc(used->id, &desc), ZX_OK);
  EXPECT_TRUE(used->len == sizeof(msg1));
  EXPECT_STREQ(reinterpret_cast<const char*>(desc.addr), msg1);

  used = frontend->rx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(frontend->rx_queue().queue()->ReadDesc(used->id, &desc), ZX_OK);
  EXPECT_TRUE(used->len == sizeof(msg2));
  EXPECT_STREQ(reinterpret_cast<const char*>(desc.addr), msg2);
}

TEST_F(TransactionTest, SendTest) {
  const auto frontend = remote_->GetFrontend(trusty_vdev_->notify_id());
  ASSERT_TRUE(frontend != nullptr);

  // Send some buffers from remote side
  const char msg1[] = "This is the first message";
  const char msg2[] = "This is the second message";

  auto buf = remote_->AllocBuffer(sizeof(msg1));
  ASSERT_TRUE(buf != nullptr);
  memcpy(buf, msg1, sizeof(msg1));
  ASSERT_EQ(frontend->tx_queue()
                .BuildDescriptor()
                .AppendReadable(buf, sizeof(msg1))
                .Build(),
            ZX_OK);

  buf = remote_->AllocBuffer(sizeof(msg2));
  ASSERT_TRUE(buf != nullptr);
  memcpy(buf, msg2, sizeof(msg2));
  ASSERT_EQ(frontend->tx_queue()
                .BuildDescriptor()
                .AppendReadable(buf, sizeof(msg2))
                .Build(),
            ZX_OK);

  loop_.RunUntilIdle();

  // Read the message from trusty virtio device and verify it
  char msg_buf[kMsgBufferSize];
  uint32_t byte_read;
  ASSERT_EQ(
      channel_.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
      ZX_OK);
  EXPECT_EQ(sizeof(msg1), byte_read);
  EXPECT_STREQ(msg_buf, msg1);

  ASSERT_EQ(
      channel_.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
      ZX_OK);
  EXPECT_EQ(sizeof(msg2), byte_read);
  EXPECT_STREQ(msg_buf, msg2);
}

TEST_F(TransactionTest, SendInvalidBuffer) {
  const auto frontend = remote_->GetFrontend(trusty_vdev_->notify_id());
  ASSERT_TRUE(frontend != nullptr);

  // Send invalid buffer from frontend (not on shared memory)
  const char invalid_buf[] = "The buffer should be dropped";
  ASSERT_EQ(
      frontend->tx_queue()
          .BuildDescriptor()
          .AppendReadable(const_cast<char*>(invalid_buf), sizeof(invalid_buf))
          .Build(),
      ZX_OK);

  loop_.RunUntilIdle();

  // Frontend should able to get the dropped tx buffer
  volatile vring_used_elem* used = frontend->tx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(used->len, 0u);

  // The channal should not have any message
  char msg_buf[kMsgBufferSize];
  uint32_t byte_read;
  ASSERT_EQ(
      channel_.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
      ZX_ERR_SHOULD_WAIT);

  // Should still able to send a valid message
  const char msg[] = "This is a valid message";
  auto valid_buf = remote_->AllocBuffer(sizeof(msg));
  ASSERT_TRUE(valid_buf != nullptr);
  memcpy(valid_buf, msg, sizeof(msg));
  ASSERT_EQ(frontend->tx_queue()
                .BuildDescriptor()
                .AppendReadable(valid_buf, sizeof(msg))
                .Build(),
            ZX_OK);

  loop_.RunUntilIdle();

  // Can get the message now
  ASSERT_EQ(
      channel_.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
      ZX_OK);
  EXPECT_EQ(sizeof(msg), byte_read);
  EXPECT_STREQ(msg_buf, msg);
}

TEST_F(TransactionTest, InvalidRxBuffer) {
  const auto frontend = remote_->GetFrontend(trusty_vdev_->notify_id());
  ASSERT_TRUE(frontend != nullptr);

  // Add an invalid rx buffer (not on shared memory)
  char invalid_buf[kMsgBufferSize];
  ASSERT_EQ(frontend->rx_queue()
                .BuildDescriptor()
                .AppendReadable(&invalid_buf, sizeof(invalid_buf))
                .Build(),
            ZX_OK);

  const char msg[] = "The message should not be received";
  ASSERT_EQ(channel_.write(0, msg, sizeof(msg), NULL, 0), ZX_OK);

  loop_.RunUntilIdle();

  // Frontend should not recevied the message
  volatile vring_used_elem* used = frontend->rx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(used->len, 0u);

  // Added another valid rx buffer
  auto valid_buf = remote_->AllocBuffer(sizeof(msg));
  ASSERT_TRUE(valid_buf != nullptr);
  ASSERT_EQ(frontend->rx_queue()
                .BuildDescriptor()
                .AppendWriteable(valid_buf, sizeof(msg))
                .Build(),
            ZX_OK);

  loop_.RunUntilIdle();

  // Can receive the message now
  virtio_desc_t desc;
  used = frontend->rx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(frontend->rx_queue().queue()->ReadDesc(used->id, &desc), ZX_OK);
  EXPECT_TRUE(used->len == sizeof(msg));
  EXPECT_STREQ(reinterpret_cast<const char*>(desc.addr), msg);
}

TEST_F(TransactionTest, SendWhileBusStopTest) {
  ASSERT_EQ(bus_->Stop(nullptr, 0), ZX_OK);

  const auto frontend = remote_->GetFrontend(trusty_vdev_->notify_id());
  ASSERT_TRUE(frontend != nullptr);

  // Send some buffers from remote side
  const char msg[] = "This is test message";

  auto buf = remote_->AllocBuffer(sizeof(msg));
  ASSERT_TRUE(buf != nullptr);
  memcpy(buf, msg, sizeof(msg));
  ASSERT_EQ(frontend->tx_queue()
                .BuildDescriptor()
                .AppendReadable(buf, sizeof(msg))
                .Build(),
            ZX_OK);

  loop_.RunUntilIdle();

  // Read message from trusty virtio device and should have no message come in
  char msg_buf[kMsgBufferSize];
  uint32_t byte_read;
  ASSERT_EQ(
      channel_.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
      ZX_ERR_SHOULD_WAIT);

  // Start virtio bus and read message again
  ASSERT_EQ(bus_->Start(rsc_table_, rsc_table_size_), ZX_OK);

  loop_.RunUntilIdle();

  ASSERT_EQ(
      channel_.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
      ZX_OK);
  EXPECT_EQ(sizeof(msg), byte_read);
  EXPECT_STREQ(msg_buf, msg);
}

}  // namespace trusty_virtio
