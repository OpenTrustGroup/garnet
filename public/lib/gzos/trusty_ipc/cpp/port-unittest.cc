// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"
#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

namespace trusty_ipc {

class TipcPortTest : public ::testing::Test {
 public:
  TipcPortTest()
      : loop_(&kAsyncLoopConfigMakeDefault), port_(kNumItems, kItemSize) {}

 protected:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;

  async::Loop loop_;
  TipcPortImpl port_;
};

TEST_F(TipcPortTest, PortConnect) {
  TipcPortPtr port_client;
  port_.Bind(port_client.NewRequest());

  fbl::RefPtr<TipcChannelImpl> channel;
  port_client->GetInfo([&channel](uint32_t num_items, uint64_t item_size) {
    ASSERT_EQ(TipcChannelImpl::Create(num_items, item_size, &channel), ZX_OK);
  });

  loop_.RunUntilIdle();

  channel->SetReadyCallback(
      [&channel]() { channel->SignalEvent(TipcEvent::READY); });

  auto local_handle = channel->GetInterfaceHandle();
  port_client->Connect(
      std::move(local_handle),
      [&channel](zx_status_t status,
                 fidl::InterfaceHandle<TipcChannel> peer_handle) {
        ASSERT_EQ(status, ZX_OK);
        channel->BindPeerInterfaceHandle(std::move(peer_handle));
      });

  loop_.RunUntilIdle();

  WaitResult result;
  EXPECT_EQ(port_.Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  fbl::RefPtr<TipcChannelImpl> remote_channel;
  EXPECT_EQ(port_.Accept(&remote_channel), ZX_OK);

  loop_.RunUntilIdle();

  EXPECT_EQ(channel->Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  // port wait again and should have no event
  EXPECT_EQ(port_.Wait(&result, zx::deadline_after(zx::msec(1))),
            ZX_ERR_TIMED_OUT);
}

}  // namespace trusty_ipc
