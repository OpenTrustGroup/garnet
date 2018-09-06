// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_layer.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel_manager.h"

namespace btlib {
namespace rfcomm {
namespace {

constexpr l2cap::ChannelId kL2CAPChannelId1 = 0x0040;
constexpr l2cap::ChannelId kL2CAPChannelId2 = 0x0041;
constexpr hci::ConnectionHandle kHandle1 = 1;

void DoNothingWithChannel(fbl::RefPtr<Channel> channel,
                          ServerChannel server_channel) {}

class RFCOMM_ChannelManagerTest : public l2cap::testing::FakeChannelTest {
 public:
  RFCOMM_ChannelManagerTest() : channel_manager_(nullptr) {}
  ~RFCOMM_ChannelManagerTest() override = default;

 protected:
  // Captures the state of the fake remote peer.
  struct PeerState {
    // Whether this peer supports credit-based flow. This bool also indicates
    // whether the session with this peer will have credit-based flow turned on;
    // our RFCOMM implementation will always turn on credit-based flow if the
    // peer supports it.
    bool credit_based_flow;
    Role role;
  };

  void SetUp() override {
    l2cap_ = l2cap::testing::FakeLayer::Create();
    ZX_DEBUG_ASSERT(l2cap_);

    l2cap_->Initialize();
    l2cap_->AddACLConnection(
        kHandle1, hci::Connection::Role::kMaster,
        [] { bt_log(WARN, "rfcomm-test", "unimplemented"); }, dispatcher());
    // Any new L2CAP channels (incoming our outgoing) opened by our
    // ChannelManager will be captured and stored in |handle_to_fake_channel_|.
    // Subsequently, all channels will have listeners attached to them, and any
    // frames sent from our RFCOMM sessions will be put into the queues in
    // |handle_to_incoming_frames_|.
    l2cap_->set_channel_callback(
        [this](fbl::RefPtr<l2cap::testing::FakeChannel> l2cap_channel) {
          ZX_DEBUG_ASSERT(l2cap_channel);
          auto handle = l2cap_channel->link_handle();
          handle_to_fake_channel_.emplace(handle, l2cap_channel);
          l2cap_channel->SetSendCallback(
              [this, handle](auto sdu) {
                if (handle_to_incoming_frames_.find(handle) ==
                    handle_to_incoming_frames_.end()) {
                  handle_to_incoming_frames_.emplace(
                      handle,
                      std::queue<std::unique_ptr<const common::ByteBuffer>>());
                }
                handle_to_incoming_frames_[handle].push(std::move(sdu));
              },
              dispatcher());
        });

    channel_manager_ = ChannelManager::Create(l2cap_.get());
    ZX_DEBUG_ASSERT(channel_manager_);
  }

  void TearDown() override {
    channel_manager_.release();
    l2cap_.reset();
    handle_to_peer_state_.clear();
    handle_to_fake_channel_.clear();
    handle_to_incoming_frames_.clear();
  }

  // Emplace a new PeerState for a new fake peer. Should be called for each fake
  // peer which a test is emulating. The returned PeerState should then be
  // updated throughout the test (e.g. the multiplexer state should change when
  // the multiplexer starts up).
  PeerState& AddFakePeerState(hci::ConnectionHandle handle, PeerState state) {
    ZX_DEBUG_ASSERT(handle_to_peer_state_.find(handle) ==
                    handle_to_peer_state_.end());
    handle_to_peer_state_.emplace(handle, std::move(state));
    return handle_to_peer_state_[handle];
  }

  fbl::RefPtr<l2cap::testing::FakeChannel> GetFakeChannel(
      hci::ConnectionHandle handle) {
    ZX_DEBUG_ASSERT(handle_to_fake_channel_.find(handle) !=
                    handle_to_fake_channel_.end());
    return handle_to_fake_channel_[handle];
  }

  void ExpectFrame(hci::ConnectionHandle handle, FrameType type, DLCI dlci) {
    auto queue_it = handle_to_incoming_frames_.find(handle);
    EXPECT_FALSE(handle_to_incoming_frames_.end() == queue_it);

    EXPECT_FALSE(handle_to_peer_state_.find(handle) ==
                 handle_to_peer_state_.end());
    const PeerState& state = handle_to_peer_state_[handle];

    auto frame = Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                              queue_it->second.front()->view());
    queue_it->second.pop();
    EXPECT_TRUE(frame);
    EXPECT_EQ(type, static_cast<FrameType>(frame->control()));
    EXPECT_EQ(dlci, frame->dlci());
  }

  void ReceiveFrame(hci::ConnectionHandle handle,
                    std::unique_ptr<Frame> frame) {
    auto channel = GetFakeChannel(handle);

    auto buffer = common::NewSlabBuffer(frame->written_size());
    frame->Write(buffer->mutable_view());
    channel->Receive(buffer->view());
  }

  // Makes the asynchronous channel-getting process synchronous, for the
  // purposes of writing clean tests. This function will imitiate (to
  // |channel_manager_|) an RFCOMM peer, and will send frames to handle
  // multiplexer startup, optional parameter negotiation, and finally, channel
  // opening.
  //
  // If this is the first channel which will be opened on this handle,
  // the state of the peer should first be set in |handle_to_peer_state_|.
  // These state variables will then be used during the session startup
  // procedure which will ensue.
  fbl::RefPtr<Channel> OpenOutgoingChannel(hci::ConnectionHandle handle,
                                           ServerChannel server_channel);

  // The fake remote peer represented by |handle| attempts to open
  // |server_channel|. If no session exists, this function will handle session
  // startup, multiplexer startup, and parameter negotiation.
  void OpenIncomingChannel(hci::ConnectionHandle handle,
                           ServerChannel server_channel);

  std::unique_ptr<ChannelManager> channel_manager_;

  fbl::RefPtr<l2cap::testing::FakeLayer> l2cap_;

  std::unordered_map<hci::ConnectionHandle,
                     std::queue<std::unique_ptr<const common::ByteBuffer>>>
      handle_to_incoming_frames_;

  // Maps remote peers (represented as connection handles) to the L2CAP channel
  // (actually a FakeChannel) over which the corresponding RFCOMM session
  // communicates with that peer.
  std::unordered_map<hci::ConnectionHandle,
                     fbl::RefPtr<l2cap::testing::FakeChannel>>
      handle_to_fake_channel_;

  // Holds the state of the fake peers. Tests must manually update this
  // information as needed; for example, if a test mimics mux startup manually,
  // it must change its role accordingly, otherwise utility functions like
  // ExpectFrame() will not parse frames correctly.
  std::unordered_map<hci::ConnectionHandle, PeerState> handle_to_peer_state_;
};

fbl::RefPtr<Channel> RFCOMM_ChannelManagerTest::OpenOutgoingChannel(
    hci::ConnectionHandle handle, ServerChannel server_channel) {
  ZX_DEBUG_ASSERT_MSG(
      handle_to_peer_state_.find(handle) != handle_to_peer_state_.end(),
      "no peer state set for handle %#.4x", handle);
  PeerState& state = handle_to_peer_state_[handle];

  fbl::RefPtr<Channel> return_channel = nullptr;
  channel_manager_->OpenRemoteChannel(
      handle, server_channel,
      [&return_channel](auto channel, auto server_channel) {
        return_channel = channel;
      },
      dispatcher());
  RunLoopUntilIdle();

  // If the fake L2CAP channel doesn't exist yet, we need to trigger its
  // creation with TriggerOutboundChannel.
  if (handle_to_fake_channel_.find(handle) == handle_to_fake_channel_.end()) {
    l2cap_->TriggerOutboundChannel(handle, l2cap::kRFCOMM, kL2CAPChannelId1,
                                   kL2CAPChannelId2);
    RunLoopUntilIdle();
  }

  EXPECT_FALSE(handle_to_incoming_frames_.find(handle) ==
               handle_to_incoming_frames_.end());
  auto& queue = handle_to_incoming_frames_[handle];

  EXPECT_FALSE(queue.empty());
  auto frame =
      Frame::Parse(state.credit_based_flow, state.role, queue.front()->view());

  queue.pop();
  ZX_DEBUG_ASSERT(frame);

  // If we received a mux startup request, respond to it.
  if (static_cast<FrameType>(frame->control()) ==
          FrameType::kSetAsynchronousBalancedMode &&
      frame->dlci() == kMuxControlDLCI) {
    ReceiveFrame(handle, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, kMuxControlDLCI));

    state.role = Role::kResponder;

    RunLoopUntilIdle();

    // Expect that another frame has arrived.
    EXPECT_FALSE(queue.empty());
    frame = Frame::Parse(state.credit_based_flow, state.role,
                         queue.front()->view());
    queue.pop();
  }

  // If we received a parameter negotiation request, respond to it.
  if (static_cast<FrameType>(frame->control()) ==
          FrameType::kUnnumberedInfoHeaderCheck &&
      frame->dlci() == kMuxControlDLCI) {
    auto pn_command_mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              pn_command_mux_command->command_type());
    EXPECT_EQ(CommandResponse::kCommand,
              pn_command_mux_command->command_response());

    auto pn_command = std::unique_ptr<DLCParameterNegotiationCommand>(
        static_cast<DLCParameterNegotiationCommand*>(
            pn_command_mux_command.release()));

    // For now, just send back the same parameters (making sure to send the
    // correct credit-based flow response based on our credit-based flow
    // setting).
    ParameterNegotiationParams params = pn_command->params();
    params.credit_based_flow_handshake =
        state.credit_based_flow ? CreditBasedFlowHandshake::kSupportedResponse
                                : CreditBasedFlowHandshake::kUnsupported;
    ReceiveFrame(handle, std::make_unique<MuxCommandFrame>(
                             state.role, state.credit_based_flow,
                             std::make_unique<DLCParameterNegotiationCommand>(
                                 CommandResponse::kResponse, params)));

    RunLoopUntilIdle();

    // Expect that another frame has arrived.
    EXPECT_FALSE(queue.empty());
    frame = Frame::Parse(state.credit_based_flow, state.role,
                         queue.front()->view());
    queue.pop();
  }

  EXPECT_EQ(FrameType::kSetAsynchronousBalancedMode,
            FrameType(frame->control()));
  DLCI dlci = ServerChannelToDLCI(server_channel, state.role);
  EXPECT_EQ(dlci, frame->dlci());

  ReceiveFrame(handle, std::make_unique<UnnumberedAcknowledgementResponse>(
                           state.role, dlci));

  RunLoopUntilIdle();

  EXPECT_TRUE(return_channel);

  return return_channel;
}

void RFCOMM_ChannelManagerTest::OpenIncomingChannel(
    hci::ConnectionHandle handle, ServerChannel server_channel) {
  ZX_DEBUG_ASSERT_MSG(
      handle_to_peer_state_.find(handle) != handle_to_peer_state_.end(),
      "no peer state set for handle %#.4x", handle);

  PeerState& state = handle_to_peer_state_[handle];

  if (handle_to_fake_channel_.find(handle) == handle_to_fake_channel_.end()) {
    l2cap_->TriggerInboundChannel(handle, l2cap::kRFCOMM, kL2CAPChannelId1,
                                  kL2CAPChannelId2);
    RunLoopUntilIdle();

    // If channel didn't exist, then we need to do mux startup and parameter
    // negotiation.
    auto l2cap_channel = GetFakeChannel(handle);

    ReceiveFrame(handle, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             Role::kUnassigned, kMuxControlDLCI));
    RunLoopUntilIdle();
    ExpectFrame(handle, FrameType::kUnnumberedAcknowledgement, kMuxControlDLCI);
    state.role = Role::kInitiator;

    DLCI dlci = ServerChannelToDLCI(server_channel, OppositeRole(state.role));

    // Send parameter negotiation
    ParameterNegotiationParams params;
    params.dlci = dlci;
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedRequest;
    params.priority = 61;
    params.maximum_frame_size =
        l2cap_channel->rx_mtu() < l2cap_channel->tx_mtu()
            ? l2cap_channel->rx_mtu()
            : l2cap_channel->tx_mtu();
    params.initial_credits = kMaxInitialCredits;
    ReceiveFrame(handle, std::make_unique<MuxCommandFrame>(
                             state.role, state.credit_based_flow,
                             std::make_unique<DLCParameterNegotiationCommand>(
                                 CommandResponse::kCommand, params)));
    RunLoopUntilIdle();

    // Expect parameter negotiation response
    EXPECT_TRUE(handle_to_incoming_frames_[handle].size());
    auto frame =
        Frame::Parse(state.credit_based_flow, state.role,
                     handle_to_incoming_frames_[handle].front()->view());
    handle_to_incoming_frames_[handle].pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    EXPECT_EQ(kMuxControlDLCI, frame->dlci());
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());
    EXPECT_EQ(CommandResponse::kResponse, mux_command->command_response());
  }

  // Otherwise, a session must already exist with this remote peer. We can
  // furthermore assume that a channel must be open, and thus that the
  // multiplexer has also been started, and parameter negotiation is complete.

  DLCI dlci = ServerChannelToDLCI(server_channel, OppositeRole(state.role));

  // Send SABM.
  ReceiveFrame(handle, std::make_unique<SetAsynchronousBalancedModeCommand>(
                           state.role, dlci));
  RunLoopUntilIdle();

  // Expect UA response.
  ExpectFrame(handle, FrameType::kUnnumberedAcknowledgement, dlci);
}
// Expect that registration of an L2CAP channel with the Channel Manager results
// in the L2CAP channel's eventual activation.
TEST_F(RFCOMM_ChannelManagerTest, RegisterL2CAPChannel) {
  ChannelOptions l2cap_channel_options(kL2CAPChannelId1);
  auto l2cap_channel = CreateFakeChannel(l2cap_channel_options);
  EXPECT_TRUE(channel_manager_->RegisterL2CAPChannel(l2cap_channel));
  EXPECT_TRUE(l2cap_channel->activated());
}

// Test that command timeouts during multiplexer startup result in the session
// being closed down.
TEST_F(RFCOMM_ChannelManagerTest, MuxStartupAndParamNegotiation_Timeout) {
  AddFakePeerState(kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  channel_manager_->OpenRemoteChannel(kHandle1, kMinServerChannel,
                                      &DoNothingWithChannel, dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  auto channel = GetFakeChannel(kHandle1);

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Do nothing
  RunLoopFor(zx::min(5));

  // Expect closedown after timeout
  EXPECT_FALSE(channel->activated());
}

// Test successful multiplexer startup (resulting role: responder).
TEST_F(RFCOMM_ChannelManagerTest, MuxStartupAndParamNegotiation_Responder) {
  AddFakePeerState(kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  l2cap_->TriggerInboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                kL2CAPChannelId2);
  RunLoopUntilIdle();

  // Receive a multiplexer startup frame on the session
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             Role::kUnassigned, kMuxControlDLCI));
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kUnnumberedAcknowledgement, kMuxControlDLCI);
}

// Test successful multiplexer startup (resulting role: initiator)
TEST_F(RFCOMM_ChannelManagerTest, MuxStartupAndParamNegotiation_Initiator) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_received = false;
  fbl::RefPtr<Channel> channel;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel, &channel_received](auto ch, auto server_channel) {
        channel_received = true;
        channel = ch;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a UA on the session
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             Role::kUnassigned, kMuxControlDLCI));
  RunLoopUntilIdle();

  state.role = Role::kResponder;
  DLCI dlci = ServerChannelToDLCI(kMinServerChannel, state.role);

  {
    // Expect a PN command from the session
    auto queue_it = handle_to_incoming_frames_.find(kHandle1);
    EXPECT_FALSE(queue_it == handle_to_incoming_frames_.end());
    EXPECT_EQ(1ul, queue_it->second.size());
    auto frame = Frame::Parse(true, OppositeRole(state.role),
                              queue_it->second.front()->view());
    queue_it->second.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               state.role, true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode, dlci);
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, dlci));
  RunLoopUntilIdle();

  EXPECT_TRUE(channel_received);
  EXPECT_TRUE(channel);
}

// Test multiplexer startup conflict procedure (resulting role: initiator).
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_Conflict_BecomeInitiator) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_received = false;
  fbl::RefPtr<Channel> channel;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel, &channel_received](auto ch, auto server_channel) {
        channel_received = true;
        channel = ch;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a conflicting SABM on the session
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kDisconnectedMode, kMuxControlDLCI);

  // Wait and expect a SABM
  RunLoopFor(zx::sec(5));
  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a UA on the session
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  state.role = Role::kResponder;
  DLCI dlci = ServerChannelToDLCI(kMinServerChannel, state.role);

  {
    // Expect a PN command from the session
    auto queue_it = handle_to_incoming_frames_.find(kHandle1);
    EXPECT_FALSE(queue_it == handle_to_incoming_frames_.end());
    EXPECT_EQ(1ul, queue_it->second.size());
    auto frame = Frame::Parse(true, OppositeRole(state.role),
                              queue_it->second.front()->view());
    queue_it->second.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               state.role, true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode, dlci);
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, dlci));
  RunLoopUntilIdle();

  EXPECT_TRUE(channel_received);
  EXPECT_TRUE(channel);
}

// Test multiplexer startup conflict procedure (resulting role: responder).
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_Conflict_BecomeResponder) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_delivered = false;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel_delivered](auto channel, auto server_channel) {
        channel_delivered = true;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  // Expect initial mux-opening SABM
  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a conflicting SABM on the session
  state.role = Role::kNegotiating;
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  // Expect a DM frame from the session
  ExpectFrame(kHandle1, FrameType::kDisconnectedMode, kMuxControlDLCI);

  // Immediately receive another SABM on the session
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  // Expect UA
  ExpectFrame(kHandle1, FrameType::kUnnumberedAcknowledgement, kMuxControlDLCI);
  state.role = Role::kInitiator;

  {
    // Expect a PN command from the session
    EXPECT_FALSE(handle_to_incoming_frames_.find(kHandle1) ==
                 handle_to_incoming_frames_.end());
    auto& queue = handle_to_incoming_frames_[kHandle1];
    EXPECT_EQ(1ul, queue.size());
    auto frame = Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                              queue.front()->view());
    queue.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    DLCI dlci = ServerChannelToDLCI(kMinServerChannel, state.role);
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               state.role, true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  // EXPECT_TRUE(channel_received);
  // EXPECT_FALSE(channel);
}

// Tests whether sessions handle invalid max frame sizes correctly.
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_BadPN_InvalidMaxFrameSize) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_delivered = false;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel_delivered](auto channel, auto server_channel) {
        channel_delivered = true;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a UA on the session
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  state.role = Role::kResponder;
  DLCI dlci = ServerChannelToDLCI(kMinServerChannel, state.role);

  {
    // Expect a PN command from the session
    EXPECT_FALSE(handle_to_incoming_frames_.find(kHandle1) ==
                 handle_to_incoming_frames_.end());
    auto& queue = handle_to_incoming_frames_[kHandle1];
    EXPECT_EQ(1ul, queue.size());
    auto frame = Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                              queue.front()->view());
    queue.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    DLCI dlci = ServerChannelToDLCI(kMinServerChannel, state.role);
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    // Create invalid parameters.
    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;
    // Request a larger max frame size than what was proposed.
    params.maximum_frame_size += 1;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               OppositeRole(state.role), true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  ExpectFrame(kHandle1, FrameType::kDisconnect, dlci);
}

// A DM response to a mux SABM shouldn't crash (but shouldn't do anything else).
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_RejectMuxStartup) {
  AddFakePeerState(kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_delivered = false;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel_delivered](auto channel, auto server_channel) {
        channel_delivered = true;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a DM on the session
  ReceiveFrame(kHandle1, std::make_unique<DisconnectedModeResponse>(
                             Role::kUnassigned, kMuxControlDLCI));
  RunLoopUntilIdle();
}

TEST_F(RFCOMM_ChannelManagerTest, OpenOutgoingChannel) {
  handle_to_peer_state_.emplace(kHandle1, PeerState{true, Role::kUnassigned});
  PeerState& state = handle_to_peer_state_[kHandle1];

  auto channel = OpenOutgoingChannel(kHandle1, kMinServerChannel);
  EXPECT_TRUE(channel);

  DLCI dlci = ServerChannelToDLCI(kMinServerChannel, state.role);

  common::ByteBufferPtr received_data;
  channel->Activate(
      [&received_data](auto data) { received_data = std::move(data); }, []() {},
      dispatcher());

  auto pattern = common::CreateStaticByteBuffer(1, 2, 3, 4);
  auto buffer = std::make_unique<common::DynamicByteBuffer>(pattern);
  channel->Send(std::move(buffer));
  RunLoopUntilIdle();

  auto frame =
      Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                   handle_to_incoming_frames_[kHandle1].front()->view());
  EXPECT_TRUE(frame);
  EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
            static_cast<FrameType>(frame->control()));
  EXPECT_EQ(dlci, frame->dlci());
  EXPECT_EQ(pattern,
            *static_cast<UserDataFrame*>(frame.get())->TakeInformation());

  buffer = std::make_unique<common::DynamicByteBuffer>(pattern);
  ReceiveFrame(kHandle1, std::make_unique<UserDataFrame>(
                             state.role, state.credit_based_flow, dlci,
                             std::move(buffer)));
  RunLoopUntilIdle();

  EXPECT_TRUE(received_data);
  EXPECT_EQ(pattern, *received_data);
}

TEST_F(RFCOMM_ChannelManagerTest, OpenIncomingChannel) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /* credit-based flow */, Role::kUnassigned});

  fbl::RefPtr<Channel> channel;
  auto server_channel = channel_manager_->AllocateLocalChannel(
      [&channel](auto received_channel, auto) { channel = received_channel; },
      dispatcher());

  OpenIncomingChannel(kHandle1, server_channel);
  RunLoopUntilIdle();
  EXPECT_TRUE(channel);

  DLCI dlci = ServerChannelToDLCI(server_channel, OppositeRole(state.role));

  common::ByteBufferPtr received_data;
  channel->Activate(
      [&received_data](auto data) { received_data = std::move(data); }, []() {},
      dispatcher());

  auto pattern = common::CreateStaticByteBuffer(1, 2, 3, 4);
  auto buffer = std::make_unique<common::DynamicByteBuffer>(pattern);
  channel->Send(std::move(buffer));
  RunLoopUntilIdle();

  auto frame =
      Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                   handle_to_incoming_frames_[kHandle1].front()->view());
  EXPECT_TRUE(frame);
  EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
            static_cast<FrameType>(frame->control()));
  EXPECT_EQ(dlci, frame->dlci());
  EXPECT_EQ(pattern,
            *static_cast<UserDataFrame*>(frame.get())->TakeInformation());

  buffer = std::make_unique<common::DynamicByteBuffer>(pattern);
  ReceiveFrame(kHandle1, std::make_unique<UserDataFrame>(
                             state.role, state.credit_based_flow, dlci,
                             std::move(buffer)));
  RunLoopUntilIdle();

  EXPECT_TRUE(received_data);
  EXPECT_EQ(pattern, *received_data);
}

}  // namespace
}  // namespace rfcomm
}  // namespace btlib
