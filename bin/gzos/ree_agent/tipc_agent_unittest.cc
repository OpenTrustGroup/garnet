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

using namespace trusty_ipc;

namespace ree_agent {

class TaServiceFake : public TaServices {
 public:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;
  static constexpr const char* port_name = "test.svc1";

  TaServiceFake() : port_(kNumItems, kItemSize) {}

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
  void SetUp() override {
    buf_ptr_.reset(new char[PAGE_SIZE]);
    ASSERT_TRUE(buf_ptr_ != nullptr);

    ASSERT_EQ(zx::channel::create(0, &msg_cli_, &msg_srv_), ZX_OK);
    agent_ = fbl::make_unique<TipcAgent>(0, std::move(msg_srv_), PAGE_SIZE,
                                         ta_service_provider);
    ASSERT_TRUE(agent_ != nullptr);

    ASSERT_EQ(loop_.StartThread(), ZX_OK);

    StartTipcAgent();
  }

  void TearDown() override {
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

  zx_status_t SendConnectRequest(void* buf, uint32_t src, const char* name) {
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

  void VerifyConnectResponse(void* buf, uint32_t buf_size, uint32_t src,
                             zx_status_t status, uint32_t max_msg_cnt,
                             uint32_t max_msg_size) {
    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
    auto conn_res = reinterpret_cast<tipc_conn_rsp_body*>(ctrl_msg + 1);

    uint32_t expect = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_res);
    EXPECT_EQ(buf_size, expect);

    EXPECT_EQ(hdr->src, kTipcCtrlAddress);
    EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
    EXPECT_EQ(ctrl_msg->type, CtrlMessage::CONNECT_RESPONSE);
    EXPECT_EQ(ctrl_msg->body_len, sizeof(*conn_res));
    EXPECT_EQ(conn_res->target, src);
    if (conn_res->status == ZX_OK) {
      EXPECT_GE(conn_res->remote, kTipcAddrBase);
    } else {
      EXPECT_EQ(conn_res->status, static_cast<uint32_t>(ZX_ERR_NO_RESOURCES));
      EXPECT_EQ(conn_res->remote, 0u);
    }
    EXPECT_EQ(conn_res->max_msg_cnt, max_msg_cnt);
    EXPECT_EQ(conn_res->max_msg_size, max_msg_size);
  }

  fbl::unique_ptr<char> buf_ptr_;
  async::Loop loop_;
  zx::channel msg_cli_, msg_srv_;
  fbl::unique_ptr<TipcAgent> agent_;
  TaServiceFake ta_service_provider;
};

TEST_F(TipcAgentTest, PortConnectOk) {
  void* buf = static_cast<void*>(buf_ptr_.get());
  uint32_t src_addr = 5;
  ASSERT_EQ(SendConnectRequest(buf, src_addr, TaServiceFake::port_name), ZX_OK);

  loop_.RunUntilIdle();

  TipcPortImpl& port_service = ta_service_provider.port_service();
  WaitResult result;
  EXPECT_EQ(port_service.Wait(&result, zx::deadline_after(zx::sec(1))), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  fbl::RefPtr<TipcChannelImpl> remote_channel;
  EXPECT_EQ(port_service.Accept(&remote_channel), ZX_OK);

  uint32_t actual;
  ASSERT_EQ(msg_cli_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);

  VerifyConnectResponse(buf, actual, src_addr, ZX_OK, 1, kTipcChanMaxBufSize);
}

TEST_F(TipcAgentTest, PortConnectWithInvalidName) {
  void* buf = static_cast<void*>(buf_ptr_.get());
  uint32_t src_addr = 5;
  const char invalid_name[] = "invalid";
  ASSERT_EQ(SendConnectRequest(buf, src_addr, invalid_name), ZX_OK);

  uint32_t actual;
  ASSERT_EQ(msg_cli_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);

  VerifyConnectResponse(buf, actual, src_addr, ZX_ERR_NO_RESOURCES, 0, 0);
}

}  // ree_agent
