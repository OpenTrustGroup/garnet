// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>

#include "gtest/gtest.h"

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"
#include "garnet/bin/gzos/ree_agent/tipc_msg.h"

namespace ree_agent {

class ServiceFake : public TaServices {
 public:
  void ConnectToService(zx::channel ch, const std::string& service_name) {}
};

class ReeMessageTest : public ::testing::Test {
 public:
  ReeMessageTest()
      : loop_(&kAsyncLoopConfigMakeDefault), ree_message_impl_(service_fake_) {}

 protected:
  void SetUp() override {
    zx::channel ree_agent_cli, ree_agent_svc;
    ASSERT_EQ(zx::channel::create(0, &ree_agent_cli, &ree_agent_svc), ZX_OK);
    ree_message_.Bind(std::move(ree_agent_cli));
    ree_message_impl_.Bind(std::move(ree_agent_svc));

    buf_ptr_.reset(new char[PAGE_SIZE]);
    ASSERT_TRUE(buf_ptr_ != nullptr);

    ASSERT_EQ(zx::channel::create(0, &msg_local_, &msg_remote_), ZX_OK);
    ASSERT_EQ(loop_.StartThread(), ZX_OK);
  }

  void TearDown() override {
    loop_.Quit();
    loop_.JoinThreads();
  }

  zx_status_t AddMessageChannel(MessageType t, uint32_t id, zx::channel ch) {
    zx_status_t status;
    fidl::VectorPtr<MessageChannelInfo> infos;
    infos.push_back({t, id, PAGE_SIZE, std::move(ch)});

    bool ret = ree_message_->AddMessageChannel(std::move(infos), &status);
    if (!ret) {
      return ZX_ERR_INTERNAL;
    }
    return status;
  }

  ServiceFake service_fake_;
  fbl::unique_ptr<char> buf_ptr_;
  async::Loop loop_;
  ReeMessageSyncPtr ree_message_;
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

  zx_status_t status;
  ASSERT_TRUE(ree_message_->Start(nullptr, &status));
  ASSERT_EQ(status, ZX_OK);

  uint32_t expect = sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr);
  uint32_t actual;

  auto buf = buf_ptr_.get();
  ASSERT_EQ(msg_local_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);
  ASSERT_EQ(actual, expect);

  auto hdr = reinterpret_cast<tipc_hdr*>(buf);
  EXPECT_EQ(hdr->src, kTipcCtrlAddress);
  EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
  EXPECT_EQ(hdr->len, sizeof(tipc_ctrl_msg_hdr));

  auto ctrl_hdr = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr + 1);
  EXPECT_EQ(ctrl_hdr->type, CtrlMessage::GO_ONLINE);
  EXPECT_EQ(ctrl_hdr->body_len, 0u);
}

TEST_F(ReeMessageTest, StartInvalidMessageChannel) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  fidl::VectorPtr<uint32_t> ids;
  ids.push_back(1);

  zx_status_t status;
  ASSERT_TRUE(ree_message_->Start(fbl::move(ids), &status));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
}

TEST_F(ReeMessageTest, StartMessageChannelTwice) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  zx_status_t status;
  ASSERT_TRUE(ree_message_->Start(nullptr, &status));
  ASSERT_EQ(status, ZX_OK);

  // start message channel again and should return error
  ASSERT_TRUE(ree_message_->Start(nullptr, &status));
  ASSERT_EQ(status, ZX_ERR_BAD_STATE);
}

TEST_F(ReeMessageTest, StopMessageChannelBeforeStart) {
  ASSERT_EQ(AddMessageChannel(MessageType::Tipc, 0, std::move(msg_remote_)),
            ZX_OK);

  // if channel is not started, ignore stop action
  zx_status_t status;
  ASSERT_TRUE(ree_message_->Stop(nullptr, &status));
  ASSERT_EQ(status, ZX_OK);
}

}  // namespace ree_agent
