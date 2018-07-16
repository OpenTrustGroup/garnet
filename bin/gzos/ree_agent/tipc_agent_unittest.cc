// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>

#include "gtest/gtest.h"

#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "garnet/bin/gzos/ree_agent/tipc_agent.h"

#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

#define NUM_TEST_CHANNEL 3

using namespace trusty_ipc;

namespace ree_agent {

class TipcEndpointTableTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    ep_table_ = new TipcEndpointTable();
    ASSERT_TRUE(ep_table_ != nullptr);

    uint32_t num_items = 1;
    uint64_t item_size = 16;
    uint32_t i;

    for (i = 0; i < NUM_TEST_CHANNEL; i++) {
      src[i] = i;
      dst[i] = 0;
      ASSERT_EQ(TipcChannelImpl::Create(num_items, item_size, &ch_[i]), ZX_OK);

      ASSERT_EQ(ep_table_->AllocateSlot(src[i], ch_[i], &dst[i]), ZX_OK);
      EXPECT_EQ(dst[i], kTipcAddrBase + i);
    }
  }

  TipcEndpointTable* ep_table_;
  uint32_t src[NUM_TEST_CHANNEL];
  uint32_t dst[NUM_TEST_CHANNEL];
  fbl::RefPtr<TipcChannelImpl> ch_[NUM_TEST_CHANNEL];
};

TEST_F(TipcEndpointTableTest, Lookup) {
  uint32_t i;

  for (i = 0; i < NUM_TEST_CHANNEL; i++) {
    TipcEndpoint* ep = ep_table_->LookupByAddr(dst[i]);
    ASSERT_TRUE(ep != nullptr);
    EXPECT_EQ(ep->src_addr, src[i]);
    EXPECT_EQ(ep->channel->cookie(), ep);
  }
}

TEST_F(TipcEndpointTableTest, LookupWithInvalidAddr) {
  ASSERT_TRUE(ep_table_->LookupByAddr(kTipcAddrBase - 1) == nullptr);
}

TEST_F(TipcEndpointTableTest, FindInUse) {
  TipcEndpoint* ep0;
  TipcEndpoint* ep1;
  uint32_t slot_id;

  slot_id = 0;
  // always return the same endpoint if start slot id is the same
  ep0 = ep_table_->FindInUseSlot(slot_id);
  ASSERT_TRUE(ep0 != nullptr);
  EXPECT_EQ(ep0->src_addr, src[0]);
  EXPECT_EQ(slot_id, 0u);

  ep1 = ep_table_->FindInUseSlot(slot_id);
  ASSERT_TRUE(ep1 != nullptr);
  EXPECT_EQ(ep1->src_addr, src[0]);
  EXPECT_EQ(slot_id, 0u);

  EXPECT_EQ(ep0, ep1);

  // advance the slot id to find next in-use endpoint
  ep0 = ep_table_->FindInUseSlot(slot_id);
  ASSERT_TRUE(ep0 != nullptr);
  EXPECT_EQ(ep0->src_addr, src[0]);
  EXPECT_EQ(slot_id, 0u);

  slot_id++;
  ep1 = ep_table_->FindInUseSlot(slot_id);
  ASSERT_TRUE(ep1 != nullptr);
  EXPECT_EQ(ep1->src_addr, src[1]);
  EXPECT_EQ(slot_id, 1u);

  EXPECT_NE(ep0, ep1);

  // free second in-use endpoint slot
  ep_table_->FreeSlotByAddr(dst[1]);

  slot_id = 0;
  // start finding in-use endpoint from slot 0
  ep0 = ep_table_->FindInUseSlot(slot_id);
  ASSERT_TRUE(ep0 != nullptr);
  EXPECT_EQ(ep0->src_addr, src[0]);
  EXPECT_EQ(slot_id, 0u);

  slot_id++;
  ep1 = ep_table_->FindInUseSlot(slot_id);
  ASSERT_TRUE(ep1 != nullptr);
  EXPECT_EQ(ep1->src_addr, src[2]);
  EXPECT_EQ(slot_id, 2u);

  EXPECT_NE(ep0, ep1);

  slot_id++;
  // advance slot_id and find again. should have no in-use endpoint was found
  ASSERT_TRUE(ep_table_->FindInUseSlot(slot_id) == nullptr);
}

class TaServiceFake : public TaServices {
 public:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;
  static constexpr const char* port_name = "test.svc1";

  TaServiceFake() : port_(kNumItems, kItemSize, IPC_PORT_ALLOW_NS_CONNECT) {}

  void ConnectToService(zx::channel ch, const std::string& service_name) {
    if (service_name.compare(port_name) == 0) {
      fidl::InterfaceRequest<TipcPort> request(std::move(ch));
      port_.Bind(std::move(request));
    }
  }

  TipcPortImpl& port_service() { return port_; }

 private:
  TipcPortImpl port_;
};

class TipcAgentTest : public ::testing::Test {
 public:
  TipcAgentTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

 protected:
  virtual void SetUp() override {
    buf_ptr_.reset(new char[PAGE_SIZE]);
    ASSERT_TRUE(buf_ptr_ != nullptr);

    ASSERT_EQ(zx::channel::create(0, &msg_cli_, &msg_srv_), ZX_OK);

    ep_table_ = new TipcEndpointTable();
    ASSERT_TRUE(ep_table_ != nullptr);

    agent_ = fbl::make_unique<TipcAgent>(0, std::move(msg_srv_), PAGE_SIZE,
                                         ta_service_provider, ep_table_);
    ASSERT_TRUE(agent_ != nullptr);

    ASSERT_EQ(loop_.StartThread(), ZX_OK);

    StartTipcAgent();
  }

  virtual void TearDown() override {
    ASSERT_EQ(agent_->Stop(), ZX_OK);
    loop_.RunUntilIdle();

    loop_.Quit();
    loop_.JoinThreads();
  }

  void StartTipcAgent() {
    ASSERT_EQ(agent_->Start(), ZX_OK);

    uint32_t actual;
    auto buf = buf_ptr_.get();
    ASSERT_EQ(msg_cli_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
              ZX_OK);
    ASSERT_EQ(actual, sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr));

    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto ctrl_hdr = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr + 1);

    EXPECT_EQ(hdr->len, sizeof(tipc_ctrl_msg_hdr));
    EXPECT_EQ(ctrl_hdr->type, CtrlMessage::GO_ONLINE);
  }

  void PortConnect(uint32_t src, uint32_t* dst_out,
                   fbl::RefPtr<TipcChannelImpl>* chan_out) {
    ASSERT_EQ(SendConnectRequest(src, TaServiceFake::port_name), ZX_OK);
    loop_.RunUntilIdle();

    TipcPortImpl& port_service = ta_service_provider.port_service();
    WaitResult result;
    ASSERT_EQ(port_service.Wait(&result, zx::deadline_after(zx::sec(1))),
              ZX_OK);
    ASSERT_EQ(result.event, TipcEvent::READY);

    ASSERT_EQ(port_service.Accept(nullptr, chan_out), ZX_OK);

    VerifyConnectResponse(src, ZX_OK, 1, kTipcChanMaxBufSize, dst_out);
  }

  zx_status_t SendConnectRequest(uint32_t src, const char* name) {
    void* buf = static_cast<void*>(buf_ptr_.get());
    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
    auto conn_req = reinterpret_cast<tipc_conn_req_body*>(ctrl_msg + 1);

    uint32_t write_size = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_req);

    hdr->src = src;
    hdr->dst = kTipcCtrlAddress;
    hdr->len = sizeof(*ctrl_msg) + sizeof(*conn_req);
    ctrl_msg->type = CtrlMessage::CONNECT_REQUEST;
    ctrl_msg->body_len = sizeof(tipc_conn_req_body);
    strncpy(conn_req->name, name, kTipcMaxServerNameLength);
    conn_req->name[kTipcMaxServerNameLength - 1] = '\0';

    return msg_cli_.write(0, buf, write_size, nullptr, 0);
  }

  zx_status_t SendDisconnectRequest(uint32_t src, uint32_t dst) {
    void* buf = static_cast<void*>(buf_ptr_.get());
    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
    auto disc_req = reinterpret_cast<tipc_disc_req_body*>(ctrl_msg + 1);

    uint32_t write_size = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*disc_req);

    hdr->src = src;
    hdr->dst = kTipcCtrlAddress;
    hdr->len = sizeof(*ctrl_msg) + sizeof(*disc_req);
    ctrl_msg->type = CtrlMessage::DISCONNECT_REQUEST;
    ctrl_msg->body_len = sizeof(tipc_disc_req_body);
    disc_req->target = dst;

    return msg_cli_.write(0, buf, write_size, nullptr, 0);
  }

  void VerifyConnectResponse(uint32_t src, zx_status_t status,
                             uint32_t max_msg_cnt, uint32_t max_msg_size,
                             uint32_t* dst) {
    void* buf = static_cast<void*>(buf_ptr_.get());
    uint32_t actual;
    EXPECT_EQ(msg_cli_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
              ZX_OK);

    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
    auto conn_res = reinterpret_cast<tipc_conn_rsp_body*>(ctrl_msg + 1);

    uint32_t expect = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_res);
    EXPECT_EQ(actual, expect);

    EXPECT_EQ(hdr->src, kTipcCtrlAddress);
    EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
    EXPECT_EQ(ctrl_msg->type, CtrlMessage::CONNECT_RESPONSE);
    EXPECT_EQ(ctrl_msg->body_len, sizeof(*conn_res));
    EXPECT_EQ(conn_res->target, src);
    if (conn_res->status == ZX_OK) {
      EXPECT_GE(conn_res->remote, kTipcAddrBase);
    } else {
      EXPECT_EQ(conn_res->status, static_cast<uint32_t>(status));
      EXPECT_EQ(conn_res->remote, 0u);
    }
    EXPECT_EQ(conn_res->max_msg_cnt, max_msg_cnt);
    EXPECT_EQ(conn_res->max_msg_size, max_msg_size);

    if (dst) {
      *dst = conn_res->remote;
    }
  }

  zx_status_t SendMsgByRee(uint32_t src, uint32_t dst,
                             const char* msg, size_t len) {
    void* buf = static_cast<void*>(buf_ptr_.get());
    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto data = reinterpret_cast<void*>(hdr->data);

    uint32_t write_size = sizeof(*hdr) + len;

    hdr->src = src;
    hdr->dst = dst;
    hdr->len = len;
    memcpy(data, msg, len);

    return msg_cli_.write(0, buf, write_size, nullptr, 0);
  }

  void VerifyReplyMsg(uint32_t src, uint32_t dst,
                      const char* msg, size_t len) {
    void* buf = static_cast<void*>(buf_ptr_.get());
    uint32_t actual;
    EXPECT_EQ(msg_cli_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
              ZX_OK);

    EXPECT_EQ(actual, len + sizeof(tipc_hdr));

    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto data = reinterpret_cast<void*>(hdr->data);
    EXPECT_EQ(hdr->src, src);
    EXPECT_EQ(hdr->dst, dst);
    EXPECT_EQ(hdr->len, len);
    EXPECT_EQ(memcmp(data, msg, len), 0);
  }

  fbl::unique_ptr<char> buf_ptr_;
  async::Loop loop_;
  zx::channel msg_cli_, msg_srv_;
  TipcEndpointTable* ep_table_;
  fbl::unique_ptr<TipcAgent> agent_;
  TaServiceFake ta_service_provider;
};

TEST_F(TipcAgentTest, ConnectRequest) {
  uint32_t src1 = 3;
  uint32_t dst1 = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel1;
  PortConnect(src1, &dst1, &remote_channel1);

  TipcEndpoint* ep = ep_table_->LookupByAddr(dst1);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src1);

  uint32_t src2 = 5;
  uint32_t dst2 = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel2;
  PortConnect(src2, &dst2, &remote_channel2);

  ep = ep_table_->LookupByAddr(dst2);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src2);
}

TEST_F(TipcAgentTest, ConnectRequestWithInvalidName) {
  uint32_t src_addr = 5;
  const char invalid_name[] = "invalid";
  ASSERT_EQ(SendConnectRequest(src_addr, invalid_name), ZX_OK);

  VerifyConnectResponse(src_addr, ZX_ERR_NO_RESOURCES, 0, 0, nullptr);
}

TEST_F(TipcAgentTest, DisconnectRequest) {
  // Build first tipc channel
  uint32_t src1 = 3;
  uint32_t dst1 = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel1;
  PortConnect(src1, &dst1, &remote_channel1);

  TipcEndpoint* ep = ep_table_->LookupByAddr(dst1);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src1);

  // Build second tipc channel
  uint32_t src2 = 5;
  uint32_t dst2 = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel2;
  PortConnect(src2, &dst2, &remote_channel2);

  ep = ep_table_->LookupByAddr(dst2);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src2);

  // Disconnect first tipc channel
  ASSERT_EQ(SendDisconnectRequest(src1, dst1), ZX_OK);
  loop_.RunUntilIdle();

  WaitResult result;
  ASSERT_EQ(remote_channel1->Wait(&result, zx::deadline_after(zx::sec(1))),
            ZX_OK);
  ASSERT_EQ(result.event, TipcEvent::HUP);
  remote_channel1->Shutdown();
  loop_.RunUntilIdle();

  // First tipc channel is gone
  ep = ep_table_->LookupByAddr(dst1);
  EXPECT_TRUE(ep == nullptr);

  // Second tipc channel still in endpoint table
  ep = ep_table_->LookupByAddr(dst2);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src2);
}

TEST_F(TipcAgentTest, DisconnectAll) {
  // Build first tipc channel
  uint32_t src1 = 3;
  uint32_t dst1 = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel1;
  PortConnect(src1, &dst1, &remote_channel1);

  TipcEndpoint* ep = ep_table_->LookupByAddr(dst1);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src1);

  // Build second tipc channel
  uint32_t src2 = 5;
  uint32_t dst2 = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel2;
  PortConnect(src2, &dst2, &remote_channel2);

  ep = ep_table_->LookupByAddr(dst2);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src2);

  // Stop tipc agent and all tipc channels should be shutdown
  ASSERT_EQ(agent_->Stop(), ZX_OK);
  loop_.RunUntilIdle();

  // Remote channels should receive a HUP signal
  WaitResult result;
  ASSERT_EQ(remote_channel1->Wait(&result, zx::deadline_after(zx::sec(1))),
            ZX_OK);
  ASSERT_EQ(result.event, TipcEvent::HUP);
  remote_channel1->Shutdown();

  ASSERT_EQ(remote_channel2->Wait(&result, zx::deadline_after(zx::sec(1))),
            ZX_OK);
  ASSERT_EQ(result.event, TipcEvent::HUP);
  remote_channel2->Shutdown();

  // First tipc channel is gone
  ep = ep_table_->LookupByAddr(dst1);
  EXPECT_TRUE(ep == nullptr);

  // Second tipc channel is gone
  ep = ep_table_->LookupByAddr(dst2);
  EXPECT_TRUE(ep == nullptr);

  // No in-use endpoint
  uint32_t slot = 0;
  EXPECT_TRUE(ep_table_->FindInUseSlot(slot) == nullptr);
}

TEST_F(TipcAgentTest, RemoteChannelClose) {
  // Build tipc channel
  uint32_t src = 3;
  uint32_t dst = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel;
  PortConnect(src, &dst, &remote_channel);

  TipcEndpoint* ep = ep_table_->LookupByAddr(dst);
  ASSERT_TRUE(ep != nullptr);
  EXPECT_EQ(ep->src_addr, src);

  // Close remote channel
  remote_channel->Shutdown();
  loop_.RunUntilIdle();

  // Tipc channel should be removed from tipc endpoint table
  EXPECT_TRUE(ep_table_->LookupByAddr(dst) == nullptr);

  // Ree side should receive a tipc disconnect request
  void* buf = static_cast<void*>(buf_ptr_.get());
  uint32_t actual;
  EXPECT_EQ(msg_cli_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);

  auto hdr = reinterpret_cast<tipc_hdr*>(buf);
  auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
  auto disc_req = reinterpret_cast<tipc_disc_req_body*>(ctrl_msg + 1);

  uint32_t expect = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*disc_req);
  EXPECT_EQ(actual, expect);

  EXPECT_EQ(hdr->src, dst);
  EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
  EXPECT_EQ(ctrl_msg->type, CtrlMessage::DISCONNECT_REQUEST);
  EXPECT_EQ(ctrl_msg->body_len, sizeof(*disc_req));
  EXPECT_EQ(disc_req->target, src);
}

TEST_F(TipcAgentTest, EchoMsg) {
  // Build tipc channel
  uint32_t src = 3;
  uint32_t dst = 0;
  fbl::RefPtr<TipcChannelImpl> remote_channel;
  PortConnect(src, &dst, &remote_channel);

  constexpr const char* expect_msg = "echo test";
  uint32_t expect_len = strlen(expect_msg);
  ASSERT_EQ(SendMsgByRee(src, dst, expect_msg, expect_len), ZX_OK);
  loop_.RunUntilIdle();

  WaitResult result;
  ASSERT_EQ(remote_channel->Wait(&result, zx::deadline_after(zx::sec(1))),
            ZX_OK);
  ASSERT_EQ(result.event, TipcEvent::MSG);

  char* buf = buf_ptr_.get();
  size_t buf_size = PAGE_SIZE;
  uint32_t msg_id;
  size_t msg_len;
  ASSERT_EQ(remote_channel->GetMessage(&msg_id, &msg_len), ZX_OK);
  ASSERT_EQ(remote_channel->ReadMessage(msg_id, 0, buf, &buf_size), ZX_OK);
  ASSERT_EQ(remote_channel->PutMessage(msg_id), ZX_OK);

  EXPECT_EQ(msg_len, expect_len);
  EXPECT_EQ(buf_size, expect_len);
  EXPECT_EQ(memcmp(buf, expect_msg, expect_len), 0);

  // Echo to Ree
  remote_channel->SendMessage(buf, buf_size);
  loop_.RunUntilIdle();

  VerifyReplyMsg(dst, src, expect_msg, expect_len);
}

TEST_F(TipcAgentTest, SendMsgWithRemoteChannelNotAccepted) {
  uint32_t src = 3;
  uint32_t dst = kTipcAddrBase;

  ASSERT_EQ(SendConnectRequest(src, TaServiceFake::port_name), ZX_OK);
  loop_.RunUntilIdle();

  char msg[] = "echo test";
  uint32_t len = strlen(msg);
  void* buf = static_cast<void*>(msg);

  TipcEndpoint* ep = ep_table_->LookupByAddr(dst);
  ASSERT_TRUE(ep != nullptr);
  ASSERT_FALSE(ep->channel->is_ready());
  ASSERT_EQ(ep->channel->SendMessage(buf, len), ZX_ERR_SHOULD_WAIT);
}

}  // ree_agent
