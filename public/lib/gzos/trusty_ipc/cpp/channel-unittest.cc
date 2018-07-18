// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/gzos/trusty_ipc/cpp/channel.h"

namespace trusty_ipc {

class TipcChannelTest : public ::testing::Test {
 public:
  TipcChannelTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

 protected:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;

  struct TestMessage {
    TestMessage(uint32_t seq_num) {
      fxl::StringAppendf(&string_, "Test message %u", seq_num);
    }
    auto data() { return const_cast<char*>(string_.c_str()); }
    auto size() { return string_.length() + 1; }

   private:
    std::string string_;
  };

  virtual void SetUp() {
    ASSERT_EQ(loop_.StartThread(), ZX_OK);

    // Create pair of channel
    local_channel_ = fbl::MakeRefCounted<TipcChannelImpl>();
    ASSERT_TRUE(local_channel_ != nullptr);
    ASSERT_EQ(local_channel_->Init(kNumItems, kItemSize), ZX_OK);

    remote_channel_ = fbl::MakeRefCounted<TipcChannelImpl>();
    ASSERT_TRUE(remote_channel_ != nullptr);
    ASSERT_EQ(remote_channel_->Init(kNumItems, kItemSize), ZX_OK);

    // Bind the channel with each other
    auto handle = remote_channel_->GetInterfaceHandle();
    local_channel_->BindPeerInterfaceHandle(std::move(handle));
    handle = local_channel_->GetInterfaceHandle();
    remote_channel_->BindPeerInterfaceHandle(std::move(handle));

    local_channel_->NotifyReady();
  }

  virtual void TeadDown() {
    loop_.Quit();
    loop_.JoinThreads();
  }

  void TestSendAndReceive(TipcChannelImpl* sender, TipcChannelImpl* receiver) {
    ASSERT_TRUE(sender->is_bound());
    ASSERT_TRUE(receiver->is_bound());

    // Send test messages from sender
    for (uint32_t i = 0; i < kNumItems; i++) {
      TestMessage test_msg(i);
      EXPECT_EQ(sender->SendMessage(test_msg.data(), test_msg.size()), ZX_OK);
    }

    // We ran out of free buffer, should fail
    uint32_t dummy;
    EXPECT_EQ(sender->SendMessage(&dummy, sizeof(dummy)), ZX_ERR_NO_MEMORY);

    // Read test messages from receiver and verify it
    for (uint32_t i = 0; i < kNumItems; i++) {
      TestMessage expected_msg(i);

      // Get a message from the filled list
      uint32_t msg_id;
      size_t msg_len;
      EXPECT_EQ(receiver->GetMessage(&msg_id, &msg_len), ZX_OK);
      EXPECT_EQ(msg_len, expected_msg.size());

      // Read the message and verify it
      char msg_buf[msg_len];
      EXPECT_EQ(receiver->ReadMessage(msg_id, 0, msg_buf, &msg_len), ZX_OK);
      EXPECT_STREQ(msg_buf, expected_msg.data());
      EXPECT_EQ(msg_len, expected_msg.size());

      // Put the message back to free list
      EXPECT_EQ(receiver->PutMessage(msg_id), ZX_OK);
    }
  }

  async::Loop loop_;
  fbl::RefPtr<TipcChannelImpl> local_channel_;
  fbl::RefPtr<TipcChannelImpl> remote_channel_;
};

TEST_F(TipcChannelTest, ExchangeMessage) {
  TestSendAndReceive(local_channel_.get(), remote_channel_.get());
  TestSendAndReceive(remote_channel_.get(), local_channel_.get());
}

}  // namespace trusty_ipc
