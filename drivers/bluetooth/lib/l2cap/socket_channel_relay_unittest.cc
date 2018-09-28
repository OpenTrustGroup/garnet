// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel.h"

namespace btlib {
namespace l2cap {
namespace {

class L2CAP_SocketChannelRelayTest : public ::testing::Test {
 public:
  L2CAP_SocketChannelRelayTest() : loop_(&kAsyncLoopConfigAttachToThread) {
    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop_.GetState());

    constexpr ChannelId kDynamicChannelIdMin = 0x0040;
    constexpr ChannelId kRemoteChannelId = 0x0050;
    constexpr hci::ConnectionHandle kDefaultConnectionHandle = 0x0001;
    channel_ = fbl::AdoptRef(new testing::FakeChannel(
        kDynamicChannelIdMin, kRemoteChannelId, kDefaultConnectionHandle,
        hci::Connection::LinkType::kACL));
    EXPECT_TRUE(channel_);

    const auto socket_status =
        zx::socket::create(ZX_SOCKET_DATAGRAM, &local_socket_, &remote_socket_);
    local_socket_unowned_ = zx::unowned_socket(local_socket_);
    EXPECT_EQ(ZX_OK, socket_status);
  }

  // Writes data on |local_socket| until the socket is full, or an error occurs.
  // Returns the number of bytes written if the socket fills, and zero
  // otherwise.
  __WARN_UNUSED_RESULT size_t StuffSocket() {
    size_t n_total_bytes_written = 0;
    zx_status_t write_res;
    // Fill the socket buffer completely, while minimzing the number of
    // syscalls required.
    for (const auto spam_size_bytes :
         {65536, 32768, 16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32,
          16, 8, 4, 2, 1}) {
      common::DynamicByteBuffer spam_data(spam_size_bytes);
      spam_data.Fill(kSpamChar);
      do {
        size_t n_iter_bytes_written = 0;
        write_res = local_socket_unowned_->write(
            0, spam_data.data(), spam_data.size(), &n_iter_bytes_written);
        if (write_res != ZX_OK && write_res != ZX_ERR_SHOULD_WAIT) {
          bt_log(ERROR, "l2cap", "Failure in zx_socket_write(): %s",
                 zx_status_get_string(write_res));
          return 0;
        }
        n_total_bytes_written += n_iter_bytes_written;
      } while (write_res == ZX_OK);
    }
    return n_total_bytes_written;
  }

  // Reads and discards |n_bytes| on |remote_socket|. Returns true if at-least
  // |n_bytes| were successfully discarded. (The actual number of discarded
  // bytes is not known, as a pending datagram may be larger than our read
  // buffer.)
  __WARN_UNUSED_RESULT bool DiscardFromSocket(size_t n_bytes_requested) {
    common::DynamicByteBuffer received_data(n_bytes_requested);
    zx_status_t read_res;
    size_t n_total_bytes_read = 0;
    while (n_total_bytes_read < n_bytes_requested) {
      size_t n_iter_bytes_read = 0;
      read_res = remote_socket_.read(0, received_data.mutable_data(),
                                     received_data.size(), &n_iter_bytes_read);
      if (read_res != ZX_OK && read_res != ZX_ERR_SHOULD_WAIT) {
        bt_log(ERROR, "l2cap", "Failure in zx_socket_read(): %s",
               zx_status_get_string(read_res));
        return false;
      }
      if (read_res == ZX_ERR_SHOULD_WAIT) {
        EXPECT_EQ(n_bytes_requested, n_total_bytes_read);
        return false;
      } else {
        n_total_bytes_read += n_iter_bytes_read;
      }
    }
    return true;
  }

 protected:
  static constexpr auto kGoodChar = 'a';
  static constexpr auto kSpamChar = 'b';
  fbl::RefPtr<testing::FakeChannel> channel() { return channel_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  zx::socket* local_socket() { return &local_socket_; }
  zx::unowned_socket local_socket_unowned() {
    return zx::unowned_socket(local_socket_unowned_);
  }
  zx::socket* remote_socket() { return &remote_socket_; }
  zx::socket ConsumeLocalSocket() { return std::move(local_socket_); }
  void CloseRemoteSocket() { remote_socket_.reset(); }
  // Note: A call to RunLoopOnce() may cause multiple timer-based tasks
  // to be dispatched. (When the timer expires, async_loop_run_once() dispatches
  // all expired timer-based tasks.)
  void RunLoopOnce() { loop_.Run(zx::time::infinite(), true); }
  void RunLoopUntilIdle() { loop_.RunUntilIdle(); }
  void ShutdownLoop() { loop_.Shutdown(); }

 private:
  fbl::RefPtr<testing::FakeChannel> channel_;
  zx::socket local_socket_;
  zx::socket remote_socket_;
  zx::unowned_socket local_socket_unowned_;
  // TODO(NET-1526): Move to FakeChannelTest, which incorporates
  // async::TestLoop.
  async::Loop loop_;
};

class L2CAP_SocketChannelRelayLifetimeTest
    : public L2CAP_SocketChannelRelayTest {
 public:
  L2CAP_SocketChannelRelayLifetimeTest()
      : was_deactivation_callback_invoked_(false),
        relay_(std::make_unique<internal::SocketChannelRelay>(
            ConsumeLocalSocket(), channel(),
            [this](ChannelId) { was_deactivation_callback_invoked_ = true; })) {
  }

 protected:
  bool was_deactivation_callback_invoked() {
    return was_deactivation_callback_invoked_;
  }
  internal::SocketChannelRelay* relay() {
    ZX_DEBUG_ASSERT(relay_);
    return relay_.get();
  }
  void DestroyRelay() { relay_ = nullptr; }

 private:
  bool was_deactivation_callback_invoked_;
  std::unique_ptr<internal::SocketChannelRelay> relay_;
};

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       ActivateFailsIfGivenStoppedDispatcher) {
  ShutdownLoop();
  EXPECT_FALSE(relay()->Activate());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       ActivateDoesNotInvokeDeactivationCallbackOnSuccess) {
  ASSERT_TRUE(relay()->Activate());
  EXPECT_FALSE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       ActivateDoesNotInvokeDeactivationCallbackOnFailure) {
  ShutdownLoop();
  ASSERT_FALSE(relay()->Activate());
  EXPECT_FALSE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       SocketIsClosedWhenRelayIsDestroyed) {
  const char data = kGoodChar;
  ASSERT_EQ(ZX_OK, remote_socket()->write(0, &data, sizeof(data), nullptr));
  DestroyRelay();
  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            remote_socket()->write(0, &data, sizeof(data), nullptr));
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       RelayIsDeactivatedWhenDispatcherIsShutDown) {
  ASSERT_TRUE(relay()->Activate());

  ShutdownLoop();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       RelayActivationFailsIfChannelActivationFails) {
  channel()->set_activate_fails(true);
  EXPECT_FALSE(relay()->Activate());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       DestructionWithPendingSdusFromChannelDoesNotCrash) {
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'));
  DestroyRelay();
  RunLoopUntilIdle();
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       RelayIsDeactivatedWhenChannelIsClosed) {
  ASSERT_TRUE(relay()->Activate());

  channel()->Close();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       RelayIsDeactivatedWhenRemoteSocketIsClosed) {
  ASSERT_TRUE(relay()->Activate());

  CloseRemoteSocket();
  RunLoopUntilIdle();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       RelayIsDeactivatedWhenRemoteSocketIsClosedEvenWithPendingSocketData) {
  ASSERT_TRUE(relay()->Activate());
  ASSERT_TRUE(StuffSocket());

  channel()->Receive(common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'));
  RunLoopUntilIdle();
  ASSERT_FALSE(was_deactivation_callback_invoked());

  CloseRemoteSocket();
  RunLoopUntilIdle();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       OversizedDatagramDeactivatesRelay) {
  const size_t kMessageBufSize = channel()->tx_mtu() * 5;
  common::DynamicByteBuffer large_message(kMessageBufSize);
  large_message.Fill('a');
  ASSERT_TRUE(relay()->Activate());

  size_t n_bytes_written_to_socket = 0;
  const auto write_res =
      remote_socket()->write(0, large_message.data(), large_message.size(),
                             &n_bytes_written_to_socket);
  ASSERT_EQ(ZX_OK, write_res);
  ASSERT_EQ(large_message.size(), n_bytes_written_to_socket);
  RunLoopUntilIdle();

  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       SocketClosureAfterChannelClosureDoesNotHangOrCrash) {
  ASSERT_TRUE(relay()->Activate());
  channel()->Close();
  ASSERT_TRUE(was_deactivation_callback_invoked());

  CloseRemoteSocket();
  RunLoopUntilIdle();
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest,
       ChannelClosureAfterSocketClosureDoesNotHangOrCrash) {
  ASSERT_TRUE(relay()->Activate());
  CloseRemoteSocket();
  RunLoopUntilIdle();

  channel()->Close();
  ASSERT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(L2CAP_SocketChannelRelayLifetimeTest, DeactivationClosesSocket) {
  ASSERT_TRUE(relay()->Activate());
  channel()->Close();  // Triggers relay deactivation.

  const char data = kGoodChar;
  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            remote_socket()->write(0, &data, sizeof(data), nullptr));
}

class L2CAP_SocketChannelRelayDataPathTest
    : public L2CAP_SocketChannelRelayTest {
 public:
  L2CAP_SocketChannelRelayDataPathTest()
      : relay_(ConsumeLocalSocket(), channel(), nullptr /* deactivation_cb */) {
    channel()->SetSendCallback(
        [&](auto data) { sent_to_channel_.push_back(std::move(data)); },
        dispatcher());
  }

 protected:
  internal::SocketChannelRelay* relay() { return &relay_; }
  auto& sent_to_channel() { return sent_to_channel_; }

 private:
  internal::SocketChannelRelay relay_;
  std::vector<std::unique_ptr<const common::ByteBuffer>> sent_to_channel_;
};

// Fixture for tests which exercise the datapath from the controller.
class L2CAP_SocketChannelRelayRxTest
    : public L2CAP_SocketChannelRelayDataPathTest {
 protected:
  common::DynamicByteBuffer ReadDatagramFromSocket(const size_t dgram_len) {
    common::DynamicByteBuffer socket_read_buffer(
        dgram_len + 1);  // +1 to detect trailing garbage.
    size_t n_bytes_read = 0;
    const auto read_res =
        remote_socket()->read(0, socket_read_buffer.mutable_data(),
                              socket_read_buffer.size(), &n_bytes_read);
    if (read_res != ZX_OK) {
      bt_log(ERROR, "l2cap", "Failure in zx_socket_read(): %s",
             zx_status_get_string(read_res));
      return {};
    }
    return common::DynamicByteBuffer(
        common::BufferView(socket_read_buffer, n_bytes_read));
  }
};

TEST_F(L2CAP_SocketChannelRelayRxTest,
       MessageFromChannelIsCopiedToSocketSynchronously) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(kExpectedMessage);
  // Note: we dispatch one task, to get the data from the FakeChannel to
  // the SocketChannelRelay. We avoid RunUntilIdle(), to ensure that the
  // SocketChannelRelay immediately copies the l2cap::Channel data to the
  // zx::socket.
  RunLoopOnce();

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage, ReadDatagramFromSocket(kExpectedMessage.size())));
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       MultipleSdusFromChannelAreCopiedToSocketPreservingSduBoundaries) {
  const auto kExpectedMessage1 =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const auto kExpectedMessage2 =
      common::CreateStaticByteBuffer('g', 'o', 'o', 'd', 'b', 'y', 'e');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(kExpectedMessage1);
  channel()->Receive(kExpectedMessage2);
  RunLoopUntilIdle();

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage1, ReadDatagramFromSocket(kExpectedMessage1.size())));
  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage2, ReadDatagramFromSocket(kExpectedMessage2.size())));
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       SduFromChannelIsCopiedToSocketWhenSocketUnblocks) {
  size_t n_junk_bytes = StuffSocket();
  ASSERT_TRUE(n_junk_bytes);

  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(kExpectedMessage);
  RunLoopUntilIdle();

  ASSERT_TRUE(DiscardFromSocket(n_junk_bytes));
  RunLoopUntilIdle();
  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage, ReadDatagramFromSocket(kExpectedMessage.size())));
}

TEST_F(L2CAP_SocketChannelRelayRxTest, CanQueueAndWriteMultipleSDUs) {
  size_t n_junk_bytes = StuffSocket();
  ASSERT_TRUE(n_junk_bytes);

  const auto kExpectedMessage1 =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const auto kExpectedMessage2 =
      common::CreateStaticByteBuffer('g', 'o', 'o', 'd', 'b', 'y', 'e');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(kExpectedMessage1);
  channel()->Receive(kExpectedMessage2);
  RunLoopUntilIdle();

  ASSERT_TRUE(DiscardFromSocket(n_junk_bytes));
  // Run only one task. This verifies that the relay writes both pending SDUs in
  // one shot, rather than re-arming the async::Wait for each SDU.
  RunLoopOnce();

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage1, ReadDatagramFromSocket(kExpectedMessage1.size())));
  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage2, ReadDatagramFromSocket(kExpectedMessage2.size())));
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       CanQueueAndIncrementallyWriteMultipleSDUs) {
  // Find the socket buffer size.
  const size_t socket_buffer_size = StuffSocket();
  ASSERT_TRUE(DiscardFromSocket(socket_buffer_size));

  // Stuff the socket manually, rather than using StuffSocket(), so that we know
  // exactly how much buffer space we free, as we read datagrams out of
  // |remote_socket()|.
  constexpr size_t kLargeSduSize = 1023;
  zx_status_t write_res = ZX_ERR_INTERNAL;
  common::DynamicByteBuffer spam_sdu(kLargeSduSize);
  size_t n_junk_bytes = 0;
  size_t n_junk_datagrams = 0;
  spam_sdu.Fill('s');
  do {
    size_t n_iter_bytes_written = 0;
    write_res = local_socket_unowned()->write(
        0, spam_sdu.data(), spam_sdu.size(), &n_iter_bytes_written);
    ASSERT_TRUE(write_res == ZX_OK || write_res == ZX_ERR_SHOULD_WAIT)
        << "Failure in zx_socket_write: " << zx_status_get_string(write_res);
    if (write_res == ZX_OK) {
      ASSERT_EQ(spam_sdu.size(), n_iter_bytes_written);
      n_junk_bytes += spam_sdu.size();
      n_junk_datagrams += 1;
    }
  } while (write_res == ZX_OK);
  ASSERT_NE(socket_buffer_size, n_junk_bytes)
      << "Need non-zero free space in socket buffer.";

  common::DynamicByteBuffer hello_sdu(kLargeSduSize);
  common::DynamicByteBuffer goodbye_sdu(kLargeSduSize);
  hello_sdu.Fill('h');
  goodbye_sdu.Fill('g');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(hello_sdu);
  channel()->Receive(goodbye_sdu);
  // TODO(NET-1446): Replace all of the RunLoopOnce() calls below with
  // RunLoopUntilIdle().
  RunLoopOnce();

  // Free up space for just the first SDU.
  ASSERT_TRUE(common::ContainersEqual(spam_sdu,
                                      ReadDatagramFromSocket(spam_sdu.size())));
  n_junk_datagrams -= 1;
  RunLoopOnce();

  // Free up space for just the second SDU.
  ASSERT_TRUE(common::ContainersEqual(spam_sdu,
                                      ReadDatagramFromSocket(spam_sdu.size())));
  n_junk_datagrams -= 1;
  RunLoopOnce();

  // Discard spam.
  while (n_junk_datagrams) {
    ASSERT_TRUE(common::ContainersEqual(
        spam_sdu, ReadDatagramFromSocket(spam_sdu.size())));
    n_junk_datagrams -= 1;
  }

  // Read out our expected datagrams, verifying that boundaries are preserved.
  EXPECT_TRUE(common::ContainersEqual(
      hello_sdu, ReadDatagramFromSocket(hello_sdu.size())));
  EXPECT_TRUE(common::ContainersEqual(
      goodbye_sdu, ReadDatagramFromSocket(goodbye_sdu.size())));
  EXPECT_EQ(0u, ReadDatagramFromSocket(1u).size())
      << "Found unexpected datagram";
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       SdusReceivedBeforeChannelActivationAreCopiedToSocket) {
  const auto kExpectedMessage1 =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const auto kExpectedMessage2 =
      common::CreateStaticByteBuffer('g', 'o', 'o', 'd', 'b', 'y', 'e');
  channel()->Receive(kExpectedMessage1);
  channel()->Receive(kExpectedMessage2);
  ASSERT_TRUE(relay()->Activate());
  // Note: we omit RunLoopOnce()/RunLoopUntilIdle(), as Channel activation
  // delivers the messages synchronously.

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage1, ReadDatagramFromSocket(kExpectedMessage1.size())));
  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage2, ReadDatagramFromSocket(kExpectedMessage2.size())));
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       SdusPendingAtChannelClosureAreCopiedToSocket) {
  ASSERT_TRUE(StuffSocket());
  ASSERT_TRUE(relay()->Activate());

  const auto kExpectedMessage1 = common::CreateStaticByteBuffer('h');
  const auto kExpectedMessage2 = common::CreateStaticByteBuffer('i');
  channel()->Receive(kExpectedMessage1);
  channel()->Receive(kExpectedMessage2);
  RunLoopUntilIdle();

  // Discard two datagrams from socket, to make room for our SDUs to be copied
  // over.
  ASSERT_NE(0u, ReadDatagramFromSocket(1u).size());
  ASSERT_NE(0u, ReadDatagramFromSocket(1u).size());
  channel()->Close();

  // Read past all of the spam from StuffSocket().
  common::DynamicByteBuffer dgram;
  do {
    dgram = ReadDatagramFromSocket(1u);
  } while (dgram.size() && dgram[0] == kSpamChar);

  // First non-spam message should be kExpectedMessage1, and second should be
  // kExpectedMessage2.
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage1, dgram));
  EXPECT_TRUE(
      common::ContainersEqual(kExpectedMessage2, ReadDatagramFromSocket(1u)));
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       ReceivingFromChannelBetweenSocketCloseAndCloseWaitTriggerDoesNotCrash) {
  // Note: we call Channel::Receive() first, to force FakeChannel to deliver the
  // SDU synchronously to the SocketChannelRelay. Asynchronous delivery could
  // compromise the test's validity, since that would allow OnSocketClosed() to
  // be invoked before OnChannelDataReceived().
  channel()->Receive(common::CreateStaticByteBuffer(kGoodChar));
  CloseRemoteSocket();
  ASSERT_TRUE(relay()->Activate());
}

TEST_F(
    L2CAP_SocketChannelRelayRxTest,
    SocketCloseBetweenReceivingFromChannelAndSocketWritabilityDoesNotCrashOrHang) {
  ASSERT_TRUE(relay()->Activate());

  size_t n_junk_bytes = StuffSocket();
  ASSERT_TRUE(n_junk_bytes);
  channel()->Receive(common::CreateStaticByteBuffer(kGoodChar));
  RunLoopUntilIdle();

  ASSERT_TRUE(DiscardFromSocket(n_junk_bytes));
  CloseRemoteSocket();
  RunLoopUntilIdle();
}

TEST_F(L2CAP_SocketChannelRelayRxTest,
       NoDataFromChannelIsWrittenToSocketAfterDeactivation) {
  ASSERT_TRUE(relay()->Activate());

  size_t n_junk_bytes = StuffSocket();
  ASSERT_TRUE(n_junk_bytes);

  channel()->Receive(common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'));
  RunLoopUntilIdle();

  channel()->Close();  // Triggers relay deactivation.
  ASSERT_TRUE(DiscardFromSocket(n_junk_bytes));
  RunLoopUntilIdle();

  size_t n_bytes_avail = std::numeric_limits<size_t>::max();
  const auto read_res = remote_socket()->read(0, nullptr, 0, &n_bytes_avail);
  EXPECT_EQ(ZX_OK, read_res);
  EXPECT_EQ(0u, n_bytes_avail);
}

// Alias for the fixture for tests which exercise the datapath to the
// controller.
using L2CAP_SocketChannelRelayTxTest = L2CAP_SocketChannelRelayDataPathTest;

TEST_F(L2CAP_SocketChannelRelayTxTest, SduFromSocketIsCopiedToChannel) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(relay()->Activate());

  size_t n_bytes_written = 0;
  const auto write_res = remote_socket()->write(
      0, kExpectedMessage.data(), kExpectedMessage.size(), &n_bytes_written);
  ASSERT_EQ(ZX_OK, write_res);
  ASSERT_EQ(kExpectedMessage.size(), n_bytes_written);
  RunLoopUntilIdle();

  const auto& sdus = sent_to_channel();
  ASSERT_FALSE(sdus.empty());
  EXPECT_EQ(1u, sdus.size());
  ASSERT_TRUE(sdus[0]);
  EXPECT_EQ(kExpectedMessage.size(), sdus[0]->size());
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[0]));
}

TEST_F(L2CAP_SocketChannelRelayTxTest,
       MultipleSdusFromSocketAreCopiedToChannel) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const size_t kNumMessages = 3;
  ASSERT_TRUE(relay()->Activate());

  for (size_t i = 0; i < kNumMessages; ++i) {
    size_t n_bytes_written = 0;
    const auto write_res = remote_socket()->write(
        0, kExpectedMessage.data(), kExpectedMessage.size(), &n_bytes_written);
    ASSERT_EQ(ZX_OK, write_res);
    ASSERT_EQ(kExpectedMessage.size(), n_bytes_written);
    RunLoopUntilIdle();
  }

  const auto& sdus = sent_to_channel();
  ASSERT_FALSE(sdus.empty());
  ASSERT_EQ(3U, sdus.size());
  ASSERT_TRUE(sdus[0]);
  ASSERT_TRUE(sdus[1]);
  ASSERT_TRUE(sdus[2]);
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[0]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[1]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[2]));
}

TEST_F(L2CAP_SocketChannelRelayTxTest,
       MultipleSdusAreCopiedToChannelInOneRelayTask) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const size_t kNumMessages = 3;
  ASSERT_TRUE(relay()->Activate());

  for (size_t i = 0; i < kNumMessages; ++i) {
    size_t n_bytes_written = 0;
    const auto write_res = remote_socket()->write(
        0, kExpectedMessage.data(), kExpectedMessage.size(), &n_bytes_written);
    ASSERT_EQ(ZX_OK, write_res);
    ASSERT_EQ(kExpectedMessage.size(), n_bytes_written);
  }

  RunLoopOnce();  // Runs SocketChannelRelay::OnSocketReadable().
  RunLoopOnce();  // Runs all tasks queued by FakeChannel::Send().

  const auto& sdus = sent_to_channel();
  ASSERT_FALSE(sdus.empty());
  ASSERT_EQ(3U, sdus.size());
  ASSERT_TRUE(sdus[0]);
  ASSERT_TRUE(sdus[1]);
  ASSERT_TRUE(sdus[2]);
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[0]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[1]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[2]));
}

TEST_F(L2CAP_SocketChannelRelayTxTest, OversizedSduIsDropped) {
  const size_t kMessageBufSize = channel()->tx_mtu() * 5;
  common::DynamicByteBuffer large_message(kMessageBufSize);
  large_message.Fill(kGoodChar);
  ASSERT_TRUE(relay()->Activate());

  size_t n_bytes_written_to_socket = 0;
  const auto write_res =
      remote_socket()->write(0, large_message.data(), large_message.size(),
                             &n_bytes_written_to_socket);
  ASSERT_EQ(ZX_OK, write_res);
  ASSERT_EQ(large_message.size(), n_bytes_written_to_socket);
  RunLoopUntilIdle();

  ASSERT_TRUE(sent_to_channel().empty());
}

TEST_F(L2CAP_SocketChannelRelayTxTest, ValidSduAfterOversizedSduIsIgnored) {
  const auto kSentMsg = common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(relay()->Activate());

  {
    common::DynamicByteBuffer dropped_msg(channel()->tx_mtu() + 1);
    size_t n_bytes_written = 0;
    zx_status_t write_res = ZX_ERR_INTERNAL;
    dropped_msg.Fill(kGoodChar);
    write_res = remote_socket()->write(0, dropped_msg.data(),
                                       dropped_msg.size(), &n_bytes_written);
    ASSERT_EQ(ZX_OK, write_res);
    ASSERT_EQ(dropped_msg.size(), n_bytes_written);
  }

  {
    size_t n_bytes_written = 0;
    zx_status_t write_res = ZX_ERR_INTERNAL;
    write_res = remote_socket()->write(0, kSentMsg.data(), kSentMsg.size(),
                                       &n_bytes_written);
    ASSERT_EQ(ZX_OK, write_res);
    ASSERT_EQ(kSentMsg.size(), n_bytes_written);
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(sent_to_channel().empty());
}

TEST_F(L2CAP_SocketChannelRelayTxTest,
       NewSocketDataAfterChannelClosureIsNotSentToChannel) {
  ASSERT_TRUE(relay()->Activate());
  channel()->Close();

  const char data = kGoodChar;
  const auto write_res =
      remote_socket()->write(0, &data, sizeof(data), nullptr);
  ASSERT_TRUE(write_res == ZX_OK || write_res == ZX_ERR_PEER_CLOSED)
      << ": " << zx_status_get_string(write_res);
  RunLoopUntilIdle();
  EXPECT_TRUE(sent_to_channel().empty());
}

}  // namespace
}  // namespace l2cap
}  // namespace btlib
