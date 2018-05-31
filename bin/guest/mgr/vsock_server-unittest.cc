// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/vsock_server.h"

#include <unordered_set>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"
#include "garnet/bin/guest/mgr/remote_vsock_endpoint.h"
#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

namespace guestmgr {
namespace {

class VsockServerTest : public testing::Test {
 protected:
  VsockServer server;
  async::Loop loop{&kAsyncLoopConfigMakeDefault};
};

struct TestConnection {
  zx::socket socket;
  zx_status_t status = ZX_ERR_BAD_STATE;

  fuchsia::guest::SocketConnector::ConnectCallback callback() {
    return [this](zx_status_t status, zx::socket socket) {
      this->status = status;
      this->socket = std::move(socket);
    };
  }
};

static void NoOpConnectCallback(zx_status_t status, zx::socket socket) {}

// A simple |fuchsia::guest::SocketAcceptor| that just retains a list of all
// connection requests.
class TestSocketAcceptor : public fuchsia::guest::SocketAcceptor {
 public:
  struct ConnectionRequest {
    uint32_t src_cid;
    uint32_t src_port;
    uint32_t port;
    AcceptCallback callback;
  };

  ~TestSocketAcceptor() override = default;

  std::vector<ConnectionRequest> TakeRequests() {
    return std::move(connection_requests_);
  }

  fidl::InterfaceHandle<fuchsia::guest::SocketAcceptor> NewBinding() {
    return binding_.NewBinding();
  }

  // |fuchsia::guest::SocketAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    connection_requests_.emplace_back(
        ConnectionRequest{src_cid, src_port, port, std::move(callback)});
  }

 private:
  std::vector<ConnectionRequest> connection_requests_;
  fidl::Binding<fuchsia::guest::SocketAcceptor> binding_{this};
};

class TestVsockEndpoint : public VsockEndpoint, public TestSocketAcceptor {
 public:
  TestVsockEndpoint(uint32_t cid) : VsockEndpoint(cid) {}

  // |fuchsia::guest::SocketAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    TestSocketAcceptor::Accept(src_cid, src_port, port, std::move(callback));
  }
};

TEST_F(VsockServerTest, RemoveEndpointOnDelete) {
  {
    RemoteVsockEndpoint endpoint(2);
    ASSERT_EQ(nullptr, server.FindEndpoint(2));
    ASSERT_EQ(ZX_OK, server.AddEndpoint(&endpoint));
    ASSERT_EQ(&endpoint, server.FindEndpoint(2));
  }

  // Delete |endpoint| and verify the server no longer resolves it.
  ASSERT_EQ(nullptr, server.FindEndpoint(2));
}

TEST_F(VsockServerTest, CreateEndpointDuplicateCid) {
  RemoteVsockEndpoint e1(2);
  RemoteVsockEndpoint e2(2);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&e1));
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, server.AddEndpoint(&e2));
}

// Test that endpoint with CID 2 connecting to endpoint with CID 3 gets routed
// through the SocketAcceptor for CID 3.
TEST_F(VsockServerTest, Connect) {
  RemoteVsockEndpoint cid2(2);
  RemoteVsockEndpoint cid3(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid2));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid3));

  // Setup acceptor to transfer |h2| to the caller.
  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));
  TestSocketAcceptor endpoint;
  cid3.SetSocketAcceptor(endpoint.NewBinding());
  loop.RunUntilIdle();

  // Request a connection on an arbitrary port.
  TestConnection connection;
  cid2.Connect(12345, 3, 1111, connection.callback());
  loop.RunUntilIdle();

  auto requests = endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(2u, request.src_cid);
  ASSERT_EQ(12345u, request.src_port);
  ASSERT_EQ(1111u, request.port);

  request.callback(ZX_OK, std::move(h2));
  loop.RunUntilIdle();

  // Expect |h2| to have been transferred during the connect.
  ASSERT_EQ(ZX_OK, connection.status);
  ASSERT_FALSE(h2.is_valid());
  ASSERT_TRUE(connection.socket.is_valid());
}

TEST_F(VsockServerTest, ConnectNoAcceptor) {
  VsockServer server;
  RemoteVsockEndpoint cid2(2);
  RemoteVsockEndpoint cid3(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid2));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&cid3));
  TestConnection connection;
  cid2.Connect(12345, 3, 1111, connection.callback());

  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  ASSERT_FALSE(connection.socket.is_valid());
}

TEST_F(VsockServerTest, ConnectInvalidCid) {
  RemoteVsockEndpoint endpoint(2);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&endpoint));

  TestConnection connection;
  endpoint.Connect(12345, 3, 1111, connection.callback());

  ASSERT_EQ(ZX_ERR_CONNECTION_REFUSED, connection.status);
  ASSERT_FALSE(connection.socket.is_valid());
}

TEST_F(VsockServerTest, HostConnect) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Verify the connection parameters as seen by |TestConnection| look good.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(request.src_cid, 2u);
  ASSERT_GE(request.src_port, kFirstEphemeralPort);
  ASSERT_EQ(request.port, 1111u);
}

TEST_F(VsockServerTest, HostConnectMultipleTimes) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Verify each connection has a distinct |src_port|.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(4u, requests.size());
  std::unordered_set<uint32_t> observed_ports;
  for (const auto& request : requests) {
    ASSERT_EQ(request.src_cid, 2u);
    ASSERT_GE(request.src_port, kFirstEphemeralPort);
    ASSERT_EQ(request.port, 1111u);
    ASSERT_EQ(observed_ports.find(request.src_port), observed_ports.end());
    observed_ports.insert(request.src_port);
  }
}

TEST_F(VsockServerTest, HostConnectFreeEphemeralPort) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Accept connection.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request1 = requests[0];
  ASSERT_EQ(request1.src_cid, 2u);
  ASSERT_GE(request1.src_port, kFirstEphemeralPort);
  ASSERT_EQ(request1.port, 1111u);
  zx::socket h1, h2;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2));
  request1.callback(ZX_OK, std::move(h1));

  // Attempt another connection. Since |h2| is still valid it should not reuse
  // the connection.
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request2 = requests[0];
  ASSERT_NE(request1.src_port, request2.src_port);
  ASSERT_GE(request2.src_port, kFirstEphemeralPort);

  // Close |h2|.
  h2.reset();
  loop.RunUntilIdle();

  // Attempt a final connection. Expect |src_port| from the first request to
  // be recycled.
  host_endpoint.Connect(3, 1111, NoOpConnectCallback);
  requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request3 = requests[0];
  ASSERT_EQ(request1.src_port, request3.src_port);
}

TEST_F(VsockServerTest, HostListenOnConnectPort) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));

  host_endpoint.Connect(3, 1111, NoOpConnectCallback);

  // Verify connection request was delivered.
  auto requests = test_endpoint.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(request.src_cid, 2u);
  ASSERT_GE(request.src_port, kFirstEphemeralPort);
  ASSERT_EQ(request.port, 1111u);

  // We'll now try to listen on the port that is in use for the out-bound
  // connection to (3,1111). This should fail.
  TestSocketAcceptor acceptor;
  zx_status_t status = ZX_ERR_BAD_STATE;
  host_endpoint.Listen(request.src_port, acceptor.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  loop.RunUntilIdle();
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, status);
}

TEST_F(VsockServerTest, HostListenTwice) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));
  zx_status_t status = ZX_ERR_BAD_STATE;

  // Listen 1 -- OK
  TestSocketAcceptor acceptor1;
  host_endpoint.Listen(22, acceptor1.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  loop.RunUntilIdle();
  ASSERT_EQ(ZX_OK, status);

  // Listen 2 -- Fail
  TestSocketAcceptor acceptor2;
  host_endpoint.Listen(22, acceptor2.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  loop.RunUntilIdle();
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, status);
}

TEST_F(VsockServerTest, HostListenClose) {
  HostVsockEndpoint host_endpoint(2);
  TestVsockEndpoint test_endpoint(3);
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&host_endpoint));
  ASSERT_EQ(ZX_OK, server.AddEndpoint(&test_endpoint));
  zx_status_t status = ZX_ERR_BAD_STATE;

  // Setup listener on a port.
  TestSocketAcceptor acceptor;
  host_endpoint.Listen(22, acceptor.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  loop.RunUntilIdle();
  ASSERT_EQ(ZX_OK, status);

  // Verify listener is receiving connection requests.
  TestConnection connection;
  test_endpoint.Connect(12345, 2, 22, connection.callback());
  loop.RunUntilIdle();
  auto requests = acceptor.TakeRequests();
  ASSERT_EQ(1u, requests.size());
  const auto& request = requests[0];
  ASSERT_EQ(request.src_cid, 3u);
  ASSERT_GE(request.src_port, 12345u);
  ASSERT_EQ(request.port, 22u);

  // Now close the acceptor interface.
  acceptor.NewBinding();
  loop.RunUntilIdle();

  // Verify the endpoint responded to the channel close message by freeing up
  // the port.
  TestSocketAcceptor new_acceptor;
  status = ZX_ERR_BAD_STATE;
  host_endpoint.Listen(22, new_acceptor.NewBinding(),
                       [&](zx_status_t _status) { status = _status; });
  loop.RunUntilIdle();
  ASSERT_EQ(ZX_OK, status);
}

}  // namespace
}  // namespace guestmgr
