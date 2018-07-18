// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"
#include "lib/fxl/random/uuid.h"
#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

namespace trusty_ipc {

class TipcPortTest : public ::testing::Test {
 public:
  TipcPortTest()
      : loop_(&kAsyncLoopConfigMakeDefault),
        s_port_(kNumItems, kItemSize, IPC_PORT_ALLOW_TA_CONNECT),
        ns_port_(kNumItems, kItemSize, IPC_PORT_ALLOW_NS_CONNECT) {}

 protected:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;

  async::Loop loop_;
  TipcPortImpl s_port_;
  TipcPortImpl ns_port_;
};

TEST_F(TipcPortTest, SecurePortConnect) {
  TipcPortPtr port_client;
  s_port_.Bind(port_client.NewRequest());

  fbl::RefPtr<TipcChannelImpl> channel = fbl::MakeRefCounted<TipcChannelImpl>();
  ASSERT_TRUE(channel != nullptr);

  port_client->GetInfo([&channel](uint32_t num_items, uint64_t item_size) {
    FXL_LOG(ERROR) << "num_items:" << num_items;
    ASSERT_EQ(channel->Init(num_items, item_size), ZX_OK);
  });
  loop_.RunUntilIdle();

  channel->SetReadyCallback(
      [&channel]() { channel->SignalEvent(TipcEvent::READY); });

  auto local_handle = channel->GetInterfaceHandle();
  auto uuid = fidl::StringPtr(fxl::GenerateUUID());
  port_client->Connect(
      std::move(local_handle), uuid,
      [](zx_status_t status, fidl::InterfaceHandle<TipcChannel> handle) {
        ASSERT_EQ(status, ZX_OK);
      });
  loop_.RunUntilIdle();

  WaitResult result;
  EXPECT_EQ(s_port_.Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  fbl::RefPtr<TipcChannelImpl> remote_channel;
  std::string remote_uuid;
  EXPECT_EQ(s_port_.Accept(&remote_uuid, &remote_channel), ZX_OK);
  EXPECT_STREQ(uuid->c_str(), remote_uuid.c_str());

  loop_.RunUntilIdle();

  EXPECT_EQ(channel->Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  // port wait again and should have no event
  EXPECT_EQ(s_port_.Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcPortTest, SecurePortConnectFromNonSecureClient) {
  TipcPortPtr port_client;
  s_port_.Bind(port_client.NewRequest());

  fbl::RefPtr<TipcChannelImpl> channel = fbl::MakeRefCounted<TipcChannelImpl>();
  ASSERT_TRUE(channel != nullptr);

  auto local_handle = channel->GetInterfaceHandle();
  port_client->Connect(
      std::move(local_handle), nullptr,
      [&channel](zx_status_t status,
                 fidl::InterfaceHandle<TipcChannel> peer_handle) {
        ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);
      });

  loop_.RunUntilIdle();
}

TEST_F(TipcPortTest, NonSecurePortConnectFromSecureClient) {
  TipcPortPtr port_client;
  ns_port_.Bind(port_client.NewRequest());

  fbl::RefPtr<TipcChannelImpl> channel = fbl::MakeRefCounted<TipcChannelImpl>();
  ASSERT_TRUE(channel != nullptr);

  auto local_handle = channel->GetInterfaceHandle();
  auto uuid = fidl::StringPtr(fxl::GenerateUUID());
  port_client->Connect(
      std::move(local_handle), uuid,
      [&channel](zx_status_t status,
                 fidl::InterfaceHandle<TipcChannel> peer_handle) {
        ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);
      });

  loop_.RunUntilIdle();
}

}  // namespace trusty_ipc
