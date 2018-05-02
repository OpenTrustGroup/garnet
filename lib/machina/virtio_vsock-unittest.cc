// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

namespace machina {
namespace {

static constexpr size_t kDataSize = 4;
static constexpr uint32_t kVirtioVsockHostPort = 22;
static constexpr uint32_t kVirtioVsockGuestCid = 3;
static constexpr uint32_t kVirtioVsockGuestPort = 23;
static constexpr uint16_t kVirtioVsockQueueSize = 32;

class VirtioVsockTest : public testing::Test {
 public:
  VirtioVsockTest()
      : vsock_(phys_mem_, loop_.async(), kVirtioVsockGuestCid),
        rx_queue_(vsock_.rx_queue()),
        tx_queue_(vsock_.tx_queue()) {}

  void SetUp() override {
    ASSERT_EQ(rx_queue_.Init(kVirtioVsockQueueSize), ZX_OK);
    ASSERT_EQ(tx_queue_.Init(kVirtioVsockQueueSize), ZX_OK);
  }

 protected:
  PhysMemFake phys_mem_;
  async::Loop loop_;
  VirtioVsock vsock_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;

  void VerifyHeader(virtio_vsock_hdr_t* header,
                    uint32_t host_port,
                    uint32_t guest_port,
                    uint32_t len,
                    uint16_t op,
                    uint32_t flags) {
    EXPECT_EQ(header->src_cid, kVirtioVsockHostCid);
    EXPECT_EQ(header->dst_cid, kVirtioVsockGuestCid);
    EXPECT_EQ(header->src_port, host_port);
    EXPECT_EQ(header->dst_port, guest_port);
    EXPECT_EQ(header->len, len);
    EXPECT_EQ(header->type, VIRTIO_VSOCK_TYPE_STREAM);
    EXPECT_EQ(header->op, op);
    EXPECT_EQ(header->flags, flags);
  }

  void DoReceive(virtio_vsock_hdr_t* rx_header, size_t rx_size) {
    ASSERT_EQ(
        rx_queue_.BuildDescriptor().AppendWritable(rx_header, rx_size).Build(),
        ZX_OK);

    loop_.RunUntilIdle();
  }

  void DoSend(uint32_t host_port,
              uint32_t guest_port,
              uint16_t type,
              uint16_t op) {
    virtio_vsock_hdr_t tx_header = {
        .src_cid = kVirtioVsockGuestCid,
        .dst_cid = kVirtioVsockHostCid,
        .src_port = guest_port,
        .dst_port = host_port,
        .type = type,
        .op = op,
    };
    ASSERT_EQ(tx_queue_.BuildDescriptor()
                  .AppendReadable(&tx_header, sizeof(tx_header))
                  .Build(),
              ZX_OK);

    loop_.RunUntilIdle();
  }

  void HostConnectOnPortRequest(uint32_t host_port, zx::socket socket) {
    ASSERT_EQ(vsock_.Connect(kVirtioVsockHostCid, host_port,
                             kVirtioVsockGuestPort, std::move(socket)),
              ZX_OK);

    virtio_vsock_hdr_t rx_header = {};
    DoReceive(&rx_header, sizeof(rx_header));
    VerifyHeader(&rx_header, host_port, kVirtioVsockGuestPort, 0,
                 VIRTIO_VSOCK_OP_REQUEST, 0);
  }

  void HostConnectOnPortResponse(uint32_t host_port) {
    DoSend(host_port, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
           VIRTIO_VSOCK_OP_RESPONSE);
  }

  void HostReadOnPort(uint32_t host_port, zx::socket* socket) {
    uint8_t expected_data[kDataSize] = {1, 9, 8, 5};
    size_t actual;
    ASSERT_EQ(socket->write(0, expected_data, sizeof(expected_data), &actual),
              ZX_OK);
    EXPECT_EQ(actual, kDataSize);

    uint8_t rx_buffer[sizeof(virtio_vsock_hdr_t) + kDataSize] = {};
    auto rx_header = reinterpret_cast<virtio_vsock_hdr_t*>(rx_buffer);
    DoReceive(rx_header, sizeof(rx_buffer));
    VerifyHeader(rx_header, host_port, kVirtioVsockGuestPort, 4,
                 VIRTIO_VSOCK_OP_RW, 0);

    auto rx_data = rx_buffer + sizeof(*rx_header);
    EXPECT_EQ(memcmp(rx_data, expected_data, kDataSize), 0);
  }

  void HostWriteOnPort(uint32_t host_port, zx::socket* socket) {
    uint8_t tx_buffer[sizeof(virtio_vsock_hdr_t) + kDataSize] = {};
    auto tx_header = reinterpret_cast<virtio_vsock_hdr_t*>(tx_buffer);
    *tx_header = {
        .src_cid = kVirtioVsockGuestCid,
        .dst_cid = kVirtioVsockHostCid,
        .src_port = kVirtioVsockGuestPort,
        .dst_port = host_port,
        .len = kDataSize,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = VIRTIO_VSOCK_OP_RW,
    };

    uint8_t expected_data[kDataSize] = {2, 3, 0, 1};
    auto tx_data = tx_buffer + sizeof(*tx_header);
    memcpy(tx_data, expected_data, kDataSize);
    ASSERT_EQ(tx_queue_.BuildDescriptor()
                  .AppendReadable(tx_buffer, sizeof(tx_buffer))
                  .Build(),
              ZX_OK);

    loop_.RunUntilIdle();

    uint8_t actual_data[kDataSize];
    size_t actual;
    ASSERT_EQ(socket->read(0, actual_data, sizeof(actual_data), &actual),
              ZX_OK);
    EXPECT_EQ(actual, kDataSize);
    EXPECT_EQ(memcmp(actual_data, expected_data, kDataSize), 0);
  }

  void HostConnectOnPort(uint32_t host_port) {
    zx::socket sockets[2];
    ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
    HostConnectOnPortRequest(host_port, std::move(sockets[1]));
    HostConnectOnPortResponse(host_port);
    HostReadOnPort(host_port, &sockets[0]);
  }

  void HostShutdownOnPort(uint32_t host_port, uint32_t flags) {
    virtio_vsock_hdr_t rx_header = {};
    DoReceive(&rx_header, sizeof(rx_header));
    VerifyHeader(&rx_header, host_port, kVirtioVsockGuestPort, 0,
                 VIRTIO_VSOCK_OP_SHUTDOWN, flags);
  }

  void GuestConnectOnPortRequest(uint32_t host_port) {
    DoSend(host_port, kVirtioVsockGuestPort + 200, VIRTIO_VSOCK_TYPE_STREAM,
           VIRTIO_VSOCK_OP_REQUEST);
  }

  void GuestConnectOnPortResponse(uint32_t host_port, uint16_t op) {
    virtio_vsock_hdr_t rx_header = {};
    DoReceive(&rx_header, sizeof(rx_header));
    VerifyHeader(&rx_header, host_port, kVirtioVsockGuestPort + 200, 0, op, 0);
    if (op == VIRTIO_VSOCK_OP_RST) {
      EXPECT_EQ(rx_header.buf_alloc, 0u);
    } else {
      EXPECT_GT(rx_header.buf_alloc, 0u);
    }
    EXPECT_EQ(rx_header.fwd_cnt, 0u);
  }

  void GuestConnectOnPort(uint32_t host_port) {
    GuestConnectOnPortRequest(host_port);
    GuestConnectOnPortResponse(host_port, VIRTIO_VSOCK_OP_RESPONSE);
  }
};

TEST_F(VirtioVsockTest, Connect) {
  HostConnectOnPort(kVirtioVsockHostPort);
}

TEST_F(VirtioVsockTest, ConnectMultipleTimes) {
  HostConnectOnPort(kVirtioVsockHostPort);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
  HostConnectOnPort(kVirtioVsockHostPort + 1000);
}

TEST_F(VirtioVsockTest, ConnectMultipleTimesSamePort) {
  HostConnectOnPort(kVirtioVsockHostPort);

  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  ASSERT_EQ(vsock_.Connect(kVirtioVsockHostCid, kVirtioVsockHostPort,
                           kVirtioVsockGuestPort, std::move(sockets[1])),
            ZX_ERR_ALREADY_EXISTS);
}

TEST_F(VirtioVsockTest, ConnectRefused) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));

  // Test connection reset.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_RST);
  EXPECT_FALSE(vsock_.HasConnection(kVirtioVsockHostCid, kVirtioVsockHostPort,
                                    kVirtioVsockGuestPort));
}

TEST_F(VirtioVsockTest, Listen) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  auto acceptor = [&sockets](zx::socket* socket) {
    *socket = std::move(sockets[1]);
    return ZX_OK;
  };
  ASSERT_EQ(vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort, acceptor),
            ZX_OK);

  GuestConnectOnPort(kVirtioVsockHostPort);
}

TEST_F(VirtioVsockTest, ListenMultipleTimes) {
  zx::socket sockets[4];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  ASSERT_EQ(zx::socket::create(0, &sockets[2], &sockets[3]), ZX_OK);
  auto acceptor_1 = [&sockets](zx::socket* socket) {
    *socket = std::move(sockets[1]);
    return ZX_OK;
  };
  auto acceptor_2 = [&sockets](zx::socket* socket) {
    *socket = std::move(sockets[3]);
    return ZX_OK;
  };
  ASSERT_EQ(
      vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort + 1, acceptor_1),
      ZX_OK);
  ASSERT_EQ(
      vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort + 2, acceptor_2),
      ZX_OK);

  GuestConnectOnPort(kVirtioVsockHostPort + 1);
  GuestConnectOnPort(kVirtioVsockHostPort + 2);
}

TEST_F(VirtioVsockTest, ListenMultipleTimesSamePort) {
  size_t index = 1;
  zx::socket sockets[4];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  ASSERT_EQ(zx::socket::create(0, &sockets[2], &sockets[3]), ZX_OK);
  auto acceptor = [&index, &sockets](zx::socket* socket) {
    *socket = std::move(sockets[index]);
    index += 2;
    return ZX_OK;
  };
  ASSERT_EQ(vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort, acceptor),
            ZX_OK);

  GuestConnectOnPort(kVirtioVsockHostPort);

  // Test connection request.
  GuestConnectOnPortRequest(kVirtioVsockHostPort);

  EXPECT_TRUE(vsock_.HasConnection(kVirtioVsockHostCid, kVirtioVsockHostPort,
                                   kVirtioVsockGuestPort + 200));
}

TEST_F(VirtioVsockTest, ListenRefused) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  auto acceptor = [&sockets](zx::socket* socket) {
    *socket = std::move(sockets[1]);
    return ZX_OK;
  };
  ASSERT_EQ(vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort, acceptor),
            ZX_OK);

  GuestConnectOnPortRequest(kVirtioVsockHostPort + 1);
  GuestConnectOnPortResponse(kVirtioVsockHostPort + 1, VIRTIO_VSOCK_OP_RST);
}

TEST_F(VirtioVsockTest, ConnectToNonListeningPort) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  GuestConnectOnPortRequest(kVirtioVsockHostPort);
  GuestConnectOnPortResponse(kVirtioVsockHostPort, VIRTIO_VSOCK_OP_RST);
}

TEST_F(VirtioVsockTest, AcceptFailed) {
  auto acceptor = [](zx::socket* socket_out) { return ZX_ERR_UNAVAILABLE; };
  ASSERT_EQ(vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort, acceptor),
            ZX_OK);

  GuestConnectOnPortRequest(kVirtioVsockHostPort);

  // Test acceptor failure.
  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  VerifyHeader(&rx_header, kVirtioVsockHostPort, kVirtioVsockGuestPort + 200, 0,
               VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, Reset) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  sockets[0].reset();
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);
}

TEST_F(VirtioVsockTest, ShutdownRead) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(sockets[0].write(ZX_SOCKET_SHUTDOWN_WRITE, nullptr, 0, nullptr),
            ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV);
}

TEST_F(VirtioVsockTest, ShutdownWrite) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(sockets[0].write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr),
            ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);
}

TEST_F(VirtioVsockTest, WriteAfterShutdown) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  ASSERT_EQ(sockets[0].write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr),
            ZX_OK);
  HostShutdownOnPort(kVirtioVsockHostPort, VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND);

  // Test write after shutdown.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_RW);
  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  VerifyHeader(&rx_header, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0,
               VIRTIO_VSOCK_OP_RST, 0);
}

TEST_F(VirtioVsockTest, Read) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostReadOnPort(kVirtioVsockHostPort, &sockets[0]);
  HostReadOnPort(kVirtioVsockHostPort, &sockets[0]);
}

TEST_F(VirtioVsockTest, Write) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);
  HostWriteOnPort(kVirtioVsockHostPort, &sockets[0]);
  HostWriteOnPort(kVirtioVsockHostPort, &sockets[0]);
}

TEST_F(VirtioVsockTest, MultipleConnections) {
  zx::socket a_sockets[2];
  ASSERT_EQ(zx::socket::create(0, &a_sockets[0], &a_sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort + 1000,
                           std::move(a_sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort + 1000);

  zx::socket b_sockets[2];
  ASSERT_EQ(zx::socket::create(0, &b_sockets[0], &b_sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort + 2000,
                           std::move(b_sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort + 2000);

  for (auto i = 0; i < (kVirtioVsockQueueSize / 4); i++) {
    HostReadOnPort(kVirtioVsockHostPort + 1000, &a_sockets[0]);
    HostReadOnPort(kVirtioVsockHostPort + 2000, &b_sockets[0]);
    HostWriteOnPort(kVirtioVsockHostPort + 1000, &a_sockets[0]);
    HostWriteOnPort(kVirtioVsockHostPort + 2000, &b_sockets[0]);
  }
}

TEST_F(VirtioVsockTest, CreditRequest) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  HostConnectOnPortRequest(kVirtioVsockHostPort, std::move(sockets[1]));
  HostConnectOnPortResponse(kVirtioVsockHostPort);

  // Test credit request.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, VIRTIO_VSOCK_TYPE_STREAM,
         VIRTIO_VSOCK_OP_CREDIT_REQUEST);

  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  VerifyHeader(&rx_header, kVirtioVsockHostPort, kVirtioVsockGuestPort, 0,
               VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0);
  EXPECT_GT(rx_header.buf_alloc, 0u);
  EXPECT_EQ(rx_header.fwd_cnt, 0u);
}

TEST_F(VirtioVsockTest, UnsupportedSocketType) {
  zx::socket sockets[2];
  ASSERT_EQ(zx::socket::create(0, &sockets[0], &sockets[1]), ZX_OK);
  auto acceptor = [&sockets](zx::socket* socket) {
    *socket = std::move(sockets[1]);
    return ZX_OK;
  };
  ASSERT_EQ(vsock_.Listen(kVirtioVsockHostCid, kVirtioVsockHostPort, acceptor),
            ZX_OK);

  // Test connection request with invalid type.
  DoSend(kVirtioVsockHostPort, kVirtioVsockGuestPort, UINT16_MAX,
         VIRTIO_VSOCK_OP_REQUEST);

  virtio_vsock_hdr_t rx_header = {};
  DoReceive(&rx_header, sizeof(rx_header));
  EXPECT_EQ(rx_header.src_cid, kVirtioVsockHostCid);
  EXPECT_EQ(rx_header.dst_cid, kVirtioVsockGuestCid);
  EXPECT_EQ(rx_header.src_port, kVirtioVsockHostPort);
  EXPECT_EQ(rx_header.dst_port, kVirtioVsockGuestPort);
  EXPECT_EQ(rx_header.type, VIRTIO_VSOCK_TYPE_STREAM);
  EXPECT_EQ(rx_header.op, VIRTIO_VSOCK_OP_RST);
  EXPECT_EQ(rx_header.flags, 0u);
}

}  // namespace
}  // namespace machina
