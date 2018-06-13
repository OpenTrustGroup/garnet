// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"
#include "garnet/lib/trusty/tipc_msg.h"

namespace ree_agent {

class ReeMessageTest : public ::testing::Test {
 public:
  ReeMessageTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

 protected:
  void SetUp() override {
    zx::channel h1, h2;
    zx::channel::create(0, &h1, &h2);
    ree_message_.Bind(std::move(h1));
    ree_message_impl_.Bind(std::move(h2));

    zx::channel::create(0, &msg_local_, &msg_remote_);
  }

  void TearDown() override {}

  zx_status_t AddMessageChannel(MessageType t, uint32_t id, zx::channel ch) {
    zx_status_t status = ZX_ERR_INTERNAL;

    fidl::VectorPtr<MessageChannelInfo> infos;
    infos.push_back({t, id, PAGE_SIZE, std::move(ch)});

    ree_message_->AddMessageChannel(std::move(infos),
                                    [&status](int32_t err) { status = err; });
    loop_.RunUntilIdle();
    return status;
  }

  async::Loop loop_;
  ReeMessagePtr ree_message_;
  ReeMessageImpl ree_message_impl_;
  zx::channel msg_local_, msg_remote_;
};

TEST_F(ReeMessageTest, AddMessageChannelOK) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);
}

TEST_F(ReeMessageTest, AddMessageChannelWithInvalidType) {
  MessageType type = static_cast<MessageType>(-1);
  ASSERT_EQ(AddMessageChannel(type, 0, std::move(msg_remote_)),
            ZX_ERR_NOT_SUPPORTED);
}

TEST_F(ReeMessageTest, AddMessageChannelWithInvalidId) {
  uint32_t id = kMaxMsgChannels + 1;
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, id, std::move(msg_remote_)),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(ReeMessageTest, AddMessageChannelWithSameIds) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  zx::channel::create(0, &msg_local_, &msg_remote_);
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_ERR_ALREADY_EXISTS);
}

TEST_F(ReeMessageTest, StartTipcMessageChannelOK) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  zx_status_t status = ZX_ERR_INTERNAL;
  ree_message_->Start(nullptr, [&status](int32_t err) { status = err; });
  loop_.RunUntilIdle();
  ASSERT_EQ(status, ZX_OK);

  uint32_t expect =
      sizeof(trusty::tipc_hdr) + sizeof(trusty::tipc_ctrl_msg_hdr);
  uint32_t actual;

  char buf[expect + 16];
  ASSERT_EQ(msg_local_.read(0, buf, sizeof(buf), &actual, nullptr, 0, nullptr),
            ZX_OK);
  ASSERT_EQ(actual, expect);

  auto hdr = reinterpret_cast<trusty::tipc_hdr*>(buf);
  EXPECT_EQ(hdr->src, trusty::kTipcCtrlAddress);
  EXPECT_EQ(hdr->dst, trusty::kTipcCtrlAddress);
  EXPECT_EQ(hdr->len, sizeof(trusty::tipc_ctrl_msg_hdr));

  auto ctrl_hdr = reinterpret_cast<trusty::tipc_ctrl_msg_hdr*>(hdr + 1);
  EXPECT_EQ(ctrl_hdr->type, trusty::CtrlMessage::GO_ONLINE);
  EXPECT_EQ(ctrl_hdr->body_len, 0u);
}

TEST_F(ReeMessageTest, StartInvalidMessageChannel) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<uint32_t> ids;
  ids.push_back(1);

  ree_message_->Start(fbl::move(ids), [&status](int32_t err) { status = err; });
  loop_.RunUntilIdle();
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
}

TEST_F(ReeMessageTest, StartMessageChannelTwice) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  zx_status_t status = ZX_ERR_INTERNAL;
  ree_message_->Start(nullptr, [&status](int32_t err) { status = err; });
  loop_.RunUntilIdle();
  ASSERT_EQ(status, ZX_OK);

  status = ZX_ERR_INTERNAL;
  ree_message_->Start(nullptr, [&status](int32_t err) { status = err; });
  loop_.RunUntilIdle();
  ASSERT_EQ(status, ZX_ERR_BAD_STATE);
}

TEST_F(ReeMessageTest, StopMessageChannelBeforeStart) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  zx_status_t status = ZX_ERR_INTERNAL;
  ree_message_->Stop(nullptr, [&status](int32_t err) { status = err; });
  loop_.RunUntilIdle();
  ASSERT_EQ(status, ZX_OK);
}

}  // namespace ree_agent
