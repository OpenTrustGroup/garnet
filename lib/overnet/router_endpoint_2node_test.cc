// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "router_endpoint.h"
#include "test_timer.h"

//////////////////////////////////////////////////////////////////////////////
// Two node fling

namespace overnet {
namespace router_endpoint2node {

class InProcessLink final : public Link {
 public:
  explicit InProcessLink(RouterEndpoint* src, RouterEndpoint* dest,
                         uint64_t link_id)
      : fake_metrics_(src->node_id(), dest->node_id(), 1, link_id),
        dest_(dest->router()) {
    src->RegisterPeer(dest->node_id());
  }
  void Forward(Message message) override { dest_->Forward(std::move(message)); }
  LinkMetrics GetLinkMetrics() override { return fake_metrics_; }

 private:
  const LinkMetrics fake_metrics_;
  Router* const dest_;
};

class TwoNodeFling : public ::testing::Test {
 public:
  TwoNodeFling() {
    endpoint1_.router()->RegisterLink(
        std::make_unique<InProcessLink>(&endpoint1_, &endpoint2_, 99599104));
    endpoint2_.router()->RegisterLink(
        std::make_unique<InProcessLink>(&endpoint2_, &endpoint1_, 99594576));

    while (!endpoint1_.router()->HasRouteTo(NodeId(2)) ||
           !endpoint2_.router()->HasRouteTo(NodeId(1))) {
      endpoint1_.router()->BlockUntilNoBackgroundUpdatesProcessing();
      endpoint2_.router()->BlockUntilNoBackgroundUpdatesProcessing();
      std::cout << "Waiting for route establishment: ep1->2="
                << endpoint1_.router()->HasRouteTo(NodeId(2))
                << " and ep2->1=" << endpoint2_.router()->HasRouteTo(NodeId(1))
                << "\n";
      test_timer_.StepUntilNextEvent();
    }
  }

  ~TwoNodeFling() { std::cout << "~TwoNodeFling\n"; }

  RouterEndpoint* endpoint1() { return &endpoint1_; }
  RouterEndpoint* endpoint2() { return &endpoint2_; }

 private:
  TestTimer test_timer_;
  RouterEndpoint endpoint1_{&test_timer_, NodeId(1), true};
  RouterEndpoint endpoint2_{&test_timer_, NodeId(2), true};
};

TEST_F(TwoNodeFling, NoOp) {}

TEST_F(TwoNodeFling, OneMessage) {
  bool got_push_cb = false;
  bool got_pull_cb = false;

  auto intro_status = this->endpoint1()->SendIntro(
      NodeId(2), ReliabilityAndOrdering::ReliableOrdered,
      Slice::FromStaticString("hello!"));
  ASSERT_TRUE(intro_status.is_ok()) << intro_status;
  auto stream =
      std::make_unique<RouterEndpoint::Stream>(std::move(*intro_status.get()));
  auto* op = new RouterEndpoint::SendOp(stream.get(), 4);
  op->Push(Slice::FromStaticString("abcd"),
           StatusCallback(ALLOCATED_CALLBACK,
                          [&got_push_cb, op, stream{std::move(stream)}](
                              const Status& status) mutable {
                            std::cerr << "ep1: push status=" << status << "\n";
                            EXPECT_TRUE(status.is_ok()) << status;
                            got_push_cb = true;
                            op->Close(Status::Ok(), [op]() { delete op; });
                          }));

  this->endpoint2()->RecvIntro(
      StatusOrCallback<RouterEndpoint::ReceivedIntroduction>(
          [&got_pull_cb](
              StatusOr<RouterEndpoint::ReceivedIntroduction>&& status) {
            std::cerr << "ep2: recv_intro status=" << status.AsStatus() << "\n";
            ASSERT_TRUE(status.is_ok()) << status.AsStatus();
            auto intro = std::move(*status);
            EXPECT_EQ(Slice::FromStaticString("hello!"), intro.introduction)
                << intro.introduction.AsStdString();
            auto stream = std::make_unique<RouterEndpoint::Stream>(
                std::move(intro.new_stream));
            auto* op = new RouterEndpoint::ReceiveOp(stream.get());
            op->PullAll(StatusOrCallback<std::vector<Slice>>(
                ALLOCATED_CALLBACK,
                [&got_pull_cb, stream{std::move(stream)},
                 op](const StatusOr<std::vector<Slice>>& status) mutable {
                  std::cerr << "ep2: pull_all status=" << status.AsStatus()
                            << "\n";
                  EXPECT_TRUE(status.is_ok()) << status.AsStatus();
                  auto pull_text = Slice::Join(status->begin(), status->end());
                  EXPECT_EQ(Slice::FromStaticString("abcd"), pull_text)
                      << pull_text.AsStdString();
                  delete op;
                  got_pull_cb = true;
                }));
          }));

  EXPECT_TRUE(got_push_cb);
  EXPECT_TRUE(got_pull_cb);
}

}  // namespace router_endpoint2node
}  // namespace overnet
