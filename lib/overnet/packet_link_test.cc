// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_link.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"
#include "trace_cout.h"

using testing::_;
using testing::Mock;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace packet_link_test {

static constexpr uint32_t kTestMSS = 1500;

class MockDoneCB {
 public:
  MOCK_METHOD1(Callback, void(const Status&));

  StatusCallback MakeCallback() {
    return [this](const Status& status) { this->Callback(status); };
  }
};

class MockStreamHandler : public Router::StreamHandler {
 public:
  MOCK_METHOD3(HandleMessage, void(SeqNum, TimeStamp, Slice));
  void Close(Callback<void> quiesced) override {}
};

class MockPacketLink {
 public:
  MOCK_METHOD1(Emit, void(Slice));

  LinkPtr<PacketLink> MakeLink(Router* router, NodeId peer, uint32_t mss) {
    class PacketLinkImpl final : public PacketLink {
     public:
      PacketLinkImpl(MockPacketLink* mock, Router* router, NodeId peer,
                     uint32_t mss)
          : PacketLink(router, TraceCout(router->timer()), peer, mss),
            mock_(mock) {}
      void Emit(Slice packet) { mock_->Emit(std::move(packet)); }

     private:
      MockPacketLink* mock_;
    };
    return overnet::MakeLink<PacketLinkImpl>(this, router, peer, mss);
  }
};

TEST(PacketLink, NoOp) {
  TestTimer timer;
  StrictMock<MockPacketLink> mock_link;
  Router router(&timer, TraceCout(&timer), NodeId(1), true);
  router.RegisterLink(mock_link.MakeLink(&router, NodeId(2), kTestMSS));
}

TEST(PacketLink, SendOne) {
  TestTimer timer;
  StrictMock<MockPacketLink> mock_link;

  auto verify_all = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
  };

  Router router(&timer, TraceCout(&timer), NodeId(1), true);
  router.RegisterLink(mock_link.MakeLink(&router, NodeId(2), kTestMSS));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  Slice emitted;
  EXPECT_CALL(mock_link, Emit(_)).WillOnce(SaveArg<0>(&emitted));
  router.Forward(Message{
      std::move(RoutableMessage(NodeId(1)).AddDestination(
          NodeId(2), StreamId(1), SeqNum(1, 1))),
      ForwardingPayloadFactory(Slice::FromContainer({7, 8, 9})),
      timer.Now(),
  });
  verify_all();

  EXPECT_EQ(Slice::FromContainer({0, 1, 0, 6, 0, 1, 1, 7, 8, 9}), emitted);
}

TEST(PacketLink, RecvOne) {
  TestTimer timer;
  StrictMock<MockPacketLink> mock_link;
  StrictMock<MockStreamHandler> mock_stream_handler;

  Router router(&timer, TraceCout(&timer), NodeId(2), true);
  auto link_unique = mock_link.MakeLink(&router, NodeId(1), kTestMSS);
  auto* link = link_unique.get();
  router.RegisterLink(std::move(link_unique));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }
  router.RegisterStream(NodeId(1), StreamId(1), &mock_stream_handler);

  EXPECT_CALL(mock_stream_handler, HandleMessage(_, _, _));
  link->Process(timer.Now(),
                Slice::FromContainer({0, 1, 0, 6, 0, 1, 1, 7, 8, 9}));
}

}  // namespace packet_link_test
}  // namespace overnet
