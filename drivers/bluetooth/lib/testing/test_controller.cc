// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_controller.h"

#include <zircon/status.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "lib/fxl/functional/make_copyable.h"

namespace btlib {
namespace testing {

CommandTransaction::CommandTransaction(
    const common::ByteBuffer& expected,
    const std::vector<const common::ByteBuffer*>& replies)
    : expected_(expected) {
  for (const auto* buffer : replies) {
    replies_.push(common::DynamicByteBuffer(*buffer));
  }
}

bool CommandTransaction::HasMoreResponses() const {
  return !replies_.empty();
}

common::DynamicByteBuffer CommandTransaction::PopNextReply() {
  FXL_DCHECK(HasMoreResponses());
  auto reply = std::move(replies_.front());
  replies_.pop();
  return reply;
}

TestController::TestController() : FakeControllerBase() {}

TestController::~TestController() { Stop(); }

void TestController::QueueCommandTransaction(CommandTransaction transaction) {
  cmd_transactions_.push(std::move(transaction));
}

void TestController::SetDataCallback(const DataCallback& callback,
                                     fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(callback);
  FXL_DCHECK(task_runner);
  FXL_DCHECK(!data_callback_);
  FXL_DCHECK(!data_task_runner_);

  data_callback_ = callback;
  data_task_runner_ = task_runner;
}

void TestController::SetTransactionCallback(
    const fxl::Closure& callback,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(callback);
  FXL_DCHECK(task_runner);
  FXL_DCHECK(!transaction_callback_);
  FXL_DCHECK(!transaction_task_runner_);

  transaction_callback_ = callback;
  transaction_task_runner_ = task_runner;
}

void TestController::OnCommandPacketReceived(
    const common::PacketView<hci::CommandHeader>& command_packet) {
  ASSERT_FALSE(cmd_transactions_.empty())
      << "Received unexpected command packet";

  auto& current = cmd_transactions_.front();
  ASSERT_TRUE(ContainersEqual(current.expected_, command_packet.data()));

  while (!current.replies_.empty()) {
    auto& reply = current.replies_.front();
    auto status = SendCommandChannelPacket(reply);
    ASSERT_EQ(ZX_OK, status)
        << "Failed to send reply: " << zx_status_get_string(status);
    current.replies_.pop();
  }

  cmd_transactions_.pop();
  if (transaction_callback_)
    transaction_task_runner_->PostTask(transaction_callback_);
}

void TestController::OnACLDataPacketReceived(
    const common::ByteBuffer& acl_data_packet) {
  if (!data_callback_)
    return;

  common::DynamicByteBuffer packet_copy(acl_data_packet);
  data_task_runner_->PostTask(fxl::MakeCopyable([
    packet_copy = std::move(packet_copy), cb = data_callback_
  ]() mutable { cb(packet_copy); }));
}

}  // namespace testing
}  // namespace btlib
