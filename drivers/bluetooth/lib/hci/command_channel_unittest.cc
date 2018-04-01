// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"

#include "gtest/gtest.h"

#include <lib/async/cpp/task.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace btlib {
namespace hci {
namespace {

using ::btlib::testing::CommandTransaction;

using TestingBase =
    ::btlib::testing::FakeControllerTest<::btlib::testing::TestController>;

constexpr uint8_t UpperBits(const OpCode opcode) {
  return opcode >> 8;
}

constexpr uint8_t LowerBits(const OpCode opcode) {
  return opcode & 0x00FF;
}

// A reference counted object used to verify that HCI command completion and
// status callbacks are properly cleaned up after the end of a transaction.
class TestCallbackObject
    : public fxl::RefCountedThreadSafe<TestCallbackObject> {
 public:
  explicit TestCallbackObject(const fxl::Closure& deletion_callback)
      : deletion_cb_(deletion_callback) {}

  virtual ~TestCallbackObject() { deletion_cb_(); }

 private:
  fxl::Closure deletion_cb_;
};

class CommandChannelTest : public TestingBase {
 public:
  CommandChannelTest() = default;
  ~CommandChannelTest() override = default;
};

using HCI_CommandChannelTest = CommandChannelTest;

TEST_F(HCI_CommandChannelTest, SingleRequestResponse) {
  // Set up expectations:
  // clang-format off
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // HCI_CommandComplete
  auto rsp = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      0x01,  // num_hci_command_packets (1 can be sent)
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      Status::kHardwareFailure);
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req, {&rsp}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  // Send a HCI_Reset command. We attach an instance of TestCallbackObject to
  // the callbacks to verify that it gets cleaned up as expected.
  bool test_obj_deleted = false;
  auto test_obj = fxl::MakeRefCounted<TestCallbackObject>(
      [&test_obj_deleted] { test_obj_deleted = true; });

  auto reset = CommandPacket::New(kReset);
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      std::move(reset), message_loop()->task_runner(),
      [&id, this, test_obj](CommandChannel::TransactionId callback_id,
                            const EventPacket& event) {
        EXPECT_EQ(id, callback_id);
        EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
        EXPECT_EQ(4, event.view().header().parameter_total_size);
        EXPECT_EQ(1, event.view()
                         .payload<CommandCompleteEventParams>()
                         .num_hci_command_packets);
        EXPECT_EQ(kReset, le16toh(event.view()
                                      .payload<CommandCompleteEventParams>()
                                      .command_opcode));
        EXPECT_EQ(Status::kHardwareFailure,
                  event.return_params<SimpleReturnParams>()->status);

        // Quit the message loop to continue the test.
        message_loop()->QuitNow();
      });

  test_obj = nullptr;
  EXPECT_FALSE(test_obj_deleted);
  RunMessageLoop();

  // Make sure that the I/O thread is no longer holding on to |test_obj|.
  TearDown();

  EXPECT_TRUE(test_obj_deleted);
}

TEST_F(HCI_CommandChannelTest, SingleAsynchronousRequest) {
  // Set up expectations:
  // clang-format off
  // HCI_Inquiry (general, unlimited, 1s)
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kInquiry), UpperBits(kInquiry),  // HCI_Inquiry opcode
      0x05,                                      // parameter_total_size
      0x33, 0x8B, 0x9E,                          // General Inquiry
      0x01,                                      // 1.28s
      0x00                                       // Unlimited responses
      );
  // HCI_CommandStatus
  auto rsp0 = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, 0x01, // status, num_hci_command_packets (1 can be sent)
      LowerBits(kInquiry), UpperBits(kInquiry)  // HCI_Inquiry opcode
      );
  // HCI_InquiryComplete
  auto rsp1 = common::CreateStaticByteBuffer(
      kInquiryCompleteEventCode,
      0x01,  // parameter_total_size (1 byte payload)
      Status::kSuccess);
  // clang-format on
  test_device()->QueueCommandTransaction(
      CommandTransaction(req, {&rsp0, &rsp1}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  // Send HCI_Inquiry
  CommandChannel::TransactionId id;
  int cb_count = 0;
  auto cb = [&cb_count, &id, this](CommandChannel::TransactionId callback_id,
                                   const EventPacket& event) {
    cb_count++;
    EXPECT_EQ(callback_id, id);
    if (cb_count == 1) {
      EXPECT_EQ(kCommandStatusEventCode, event.event_code());
      const auto params = event.view().payload<CommandStatusEventParams>();
      EXPECT_EQ(Status::kSuccess, params.status);
      EXPECT_EQ(kInquiry, params.command_opcode);
    } else {
      EXPECT_EQ(kInquiryCompleteEventCode, event.event_code());
      EXPECT_EQ(Status::kSuccess, event.status());
      // Quit the message loop to continue the test.
      message_loop()->QuitNow();
    }
  };

  constexpr size_t kPayloadSize = sizeof(::btlib::hci::InquiryCommandParams);
  auto packet =
      ::btlib::hci::CommandPacket::New(::btlib::hci::kInquiry, kPayloadSize);
  auto params = packet->mutable_view()
                    ->mutable_payload<::btlib::hci::InquiryCommandParams>();
  params->lap = ::btlib::hci::kGIAC;
  params->inquiry_length = 1;
  params->num_responses = 0;
  id = cmd_channel()->SendCommand(std::move(packet),
                                  message_loop()->task_runner(), cb,
                                  kInquiryCompleteEventCode);
  RunMessageLoop();
  EXPECT_EQ(2, cb_count);
}

TEST_F(HCI_CommandChannelTest, SingleRequestWithStatusResponse) {
  // Set up expectations
  // clang-format off
  // HCI_Reset for the sake of testing
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  // HCI_CommandStatus
  auto rsp = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, 0x01, // status, num_hci_command_packets (1 can be sent)
      LowerBits(kReset), UpperBits(kReset)  // HCI_Reset opcode
      );
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req, {&rsp}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  // Send HCI_Reset
  CommandChannel::TransactionId id;
  auto complete_cb = [&id, this](CommandChannel::TransactionId callback_id,
                                 const EventPacket& event) {
    EXPECT_EQ(callback_id, id);
    EXPECT_EQ(kCommandStatusEventCode, event.event_code());
    EXPECT_EQ(Status::kSuccess,
              event.view().payload<CommandStatusEventParams>().status);
    EXPECT_EQ(1, event.view()
                     .payload<CommandStatusEventParams>()
                     .num_hci_command_packets);
    EXPECT_EQ(
        kReset,
        le16toh(
            event.view().payload<CommandStatusEventParams>().command_opcode));

    // Quit the message loop to continue the test.
    message_loop()->QuitNow();
  };

  auto reset = CommandPacket::New(kReset);
  id = cmd_channel()->SendCommand(std::move(reset),
                                  message_loop()->task_runner(), complete_cb,
                                  kCommandStatusEventCode);
  RunMessageLoop();
}

// Tests:
//  - Only one HCI command sent until a status is received.
//  - Receiving a status update with a new number of packets available works.
TEST_F(HCI_CommandChannelTest, OneSentUntilStatus) {
  // Set up expectations
  // clang-format off
  // HCI_Reset for the sake of testing
  auto req1 = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  auto rsp1 = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x03,  // parameter_total_size (4 byte payload)
      0x00,  // num_hci_command_packets (None can be sent)
      LowerBits(kReset), UpperBits(kReset)  // HCI_Reset opcode
      );
  auto req2 = common::CreateStaticByteBuffer(
      LowerBits(kInquiryCancel), UpperBits(kInquiryCancel),  // HCI_InquiryCancel opcode
      0x00                                   // parameter_total_size
      );
  auto rsp2 = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x03,  // parameter_total_size (4 byte payload)
      0x01,  // num_hci_command_packets (1 can be sent)
      LowerBits(kInquiryCancel), UpperBits(kInquiryCancel)  // HCI_InquiryCancel opcode
      );
  auto rsp_commandsavail = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (3 byte payload)
      Status::kSuccess, 0x01, // status, num_hci_command_packets (1 can be sent)
      0x00, 0x00 // No associated opcode.
      );
  // clang-format on
  test_device()->QueueCommandTransaction(CommandTransaction(req1, {&rsp1}));
  test_device()->QueueCommandTransaction(CommandTransaction(req2, {&rsp2}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  CommandChannel::TransactionId reset_id, inquiry_id;
  size_t cb_event_count = 0u;
  size_t transaction_count = 0u;

  test_device()->SetTransactionCallback(
      [&transaction_count]() { transaction_count++; },
      message_loop()->task_runner());

  auto cb = [&cb_event_count](CommandChannel::TransactionId,
                              const EventPacket& event) {
    EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
    OpCode expected_opcode;
    if (cb_event_count == 0u) {
      expected_opcode = kReset;
    } else {
      expected_opcode = kInquiryCancel;
    }
    EXPECT_EQ(
        expected_opcode,
        le16toh(
            event.view().payload<CommandCompleteEventParams>().command_opcode));
    cb_event_count++;
  };

  auto reset = CommandPacket::New(kReset);
  reset_id = cmd_channel()->SendCommand(std::move(reset),
                                        message_loop()->task_runner(), cb);
  auto inquiry = CommandPacket::New(kInquiryCancel);
  inquiry_id = cmd_channel()->SendCommand(std::move(inquiry),
                                          message_loop()->task_runner(), cb);

  message_loop()->RunUntilIdle();

  EXPECT_EQ(1u, transaction_count);
  EXPECT_EQ(1u, cb_event_count);

  test_device()->SendCommandChannelPacket(rsp_commandsavail);

  message_loop()->RunUntilIdle();

  EXPECT_EQ(2u, transaction_count);
  EXPECT_EQ(2u, cb_event_count);
}

// Tests:
//  - Different opcodes can be sent concurrently
//  - Same opcodes are queued until a status opcode is sent.
TEST_F(HCI_CommandChannelTest, QueuedCommands) {
  // Set up expectations
  // clang-format off
  // HCI_Reset for the sake of testing
  auto req_reset = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  auto rsp_reset = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x03,  // parameter_total_size (4 byte payload)
      0xFF,  // num_hci_command_packets (255 can be sent)
      LowerBits(kReset), UpperBits(kReset)  // HCI_Reset opcode
      );
  auto req_inqcancel = common::CreateStaticByteBuffer(
      LowerBits(kInquiryCancel), UpperBits(kInquiryCancel),  // HCI_InquiryCancel opcode
      0x00                                   // parameter_total_size
      );
  auto rsp_inqcancel = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x03,  // parameter_total_size (4 byte payload)
      0xFF,  // num_hci_command_packets (255 can be sent)
      LowerBits(kInquiryCancel), UpperBits(kInquiryCancel)  // HCI_Reset opcode
      );
  auto rsp_commandsavail = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (3 byte payload)
      Status::kSuccess, 0xFA, // status, num_hci_command_packets (250 can be sent)
      0x00, 0x00 // No associated opcode.
      );
  // clang-format on

  // We handle our own responses to make sure commands are queued.
  test_device()->QueueCommandTransaction(CommandTransaction(req_reset, {}));
  test_device()->QueueCommandTransaction(CommandTransaction(req_inqcancel, {}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(req_reset, {&rsp_reset}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  size_t transaction_count = 0u;
  size_t reset_count = 0u;
  size_t cancel_count = 0u;

  test_device()->SetTransactionCallback(
      [&transaction_count]() { transaction_count++; },
      message_loop()->task_runner());

  auto cb = [&reset_count, &cancel_count](CommandChannel::TransactionId id,
                                          const EventPacket& event) {
    EXPECT_EQ(kCommandCompleteEventCode, event.event_code());
    auto opcode = le16toh(
        event.view().payload<CommandCompleteEventParams>().command_opcode);
    if (opcode == kReset) {
      reset_count++;
    } else if (opcode == kInquiryCancel) {
      cancel_count++;
    } else {
      EXPECT_TRUE(false) << "Unexpected opcode in command callback!";
    }
  };

  // CommandChannel only one can be sent - update num_hci_command_packets
  test_device()->SendCommandChannelPacket(rsp_commandsavail);

  auto packet = CommandPacket::New(kReset);
  cmd_channel()->SendCommand(std::move(packet), message_loop()->task_runner(),
                             cb);
  packet = CommandPacket::New(kInquiryCancel);
  cmd_channel()->SendCommand(std::move(packet), message_loop()->task_runner(),
                             cb);
  packet = CommandPacket::New(kReset);
  cmd_channel()->SendCommand(std::move(packet), message_loop()->task_runner(),
                             cb);

  message_loop()->RunUntilIdle();

  // Different opcodes can be sent without a reply
  EXPECT_EQ(2u, transaction_count);

  // Even if we get a response to one, the duplicate opcode is still queued.
  test_device()->SendCommandChannelPacket(rsp_inqcancel);
  message_loop()->RunUntilIdle();

  EXPECT_EQ(2u, transaction_count);
  EXPECT_EQ(1u, cancel_count);
  EXPECT_EQ(0u, reset_count);

  // Once we get a reset back, the second can be sent (and replied to)
  test_device()->SendCommandChannelPacket(rsp_reset);
  message_loop()->RunUntilIdle();

  EXPECT_EQ(3u, transaction_count);
  EXPECT_EQ(1u, cancel_count);
  EXPECT_EQ(2u, reset_count);
}

// Tests:
//  - Asynchronous commands are handled correctly (two callbacks, one for
//    status, one for complete)
//  - Asynchronous commands with the same event result are queued even if they
//    have different opcodes.
//  - Can't register an event handler when an asynchronous command is waiting.
TEST_F(HCI_CommandChannelTest, AsynchronousCommands) {
  constexpr EventCode kTestEventCode0 = 0xFE;
  // Set up expectations
  // clang-format off
  // Using HCI_Reset for testing.
  auto req_reset = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  auto rsp_resetstatus = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, 0xFA, // status, num_hci_command_packets (250 can be sent)
      LowerBits(kReset), UpperBits(kReset)  // HCI_Reset opcode
      );
  auto req_inqcancel = common::CreateStaticByteBuffer(
      LowerBits(kInquiryCancel), UpperBits(kInquiryCancel),  // HCI_InquiryCancel opcode
      0x00                                   // parameter_total_size
      );
  auto rsp_inqstatus = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, 0xFA, // status, num_hci_command_packets (250 can be sent)
      LowerBits(kInquiryCancel), UpperBits(kInquiryCancel)  // HCI_Reset opcode
      );
  auto rsp_bogocomplete = common::CreateStaticByteBuffer(
      kTestEventCode0,
      0x00 // parameter_total_size (no payload)
      );
  // clang-format on

  test_device()->QueueCommandTransaction(
      CommandTransaction(req_reset, {&rsp_resetstatus}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(req_inqcancel, {&rsp_inqstatus}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  CommandChannel::TransactionId id1, id2;
  size_t cb_count = 0u;

  auto cb = [&id1, &id2, &cb_count, kTestEventCode0](
                CommandChannel::TransactionId callback_id,
                const EventPacket& event) {
    if (cb_count < 2) {
      EXPECT_EQ(id1, callback_id);
    } else {
      EXPECT_EQ(id2, callback_id);
    }
    if ((cb_count % 2) == 0) {
      EXPECT_EQ(kCommandStatusEventCode, event.event_code());
      auto params = event.view().payload<CommandStatusEventParams>();
      EXPECT_EQ(Status::kSuccess, params.status);
    } else if ((cb_count % 2) == 1) {
      EXPECT_EQ(kTestEventCode0, event.event_code());
    }
    cb_count++;
  };

  auto packet = CommandPacket::New(kReset);
  id1 = cmd_channel()->SendCommand(
      std::move(packet), message_loop()->task_runner(), cb, kTestEventCode0);

  message_loop()->RunUntilIdle();

  // Should have received the Status but not the result.
  EXPECT_EQ(1u, cb_count);

  // Setting another event up with different opcode will still queue the command
  // because we don't want to have two commands waiting on an event.
  packet = CommandPacket::New(kInquiryCancel);
  id2 = cmd_channel()->SendCommand(
      std::move(packet), message_loop()->task_runner(), cb, kTestEventCode0);
  RunUntilIdle();

  EXPECT_EQ(1u, cb_count);

  // Sending the complete will release the queue and send the next command.
  test_device()->SendCommandChannelPacket(rsp_bogocomplete);
  RunUntilIdle();

  EXPECT_EQ(3u, cb_count);

  // Should not be able to regisrer an event handler now, we're still waiting on
  // the asynchronous command.
  auto event_id0 = cmd_channel()->AddEventHandler(
      kTestEventCode0, [](const auto&) {}, message_loop()->task_runner());
  EXPECT_EQ(0u, event_id0);

  // Finish out the commands.
  test_device()->SendCommandChannelPacket(rsp_bogocomplete);
  RunUntilIdle();

  EXPECT_EQ(4u, cb_count);
}

// Tests:
//  - Updating to say no commands can be sent works. (commands are queued)
//  - Can't add an event handler once a SendCommand() succeeds watiing on
//    the same event code. (even if they are queued)
TEST_F(HCI_CommandChannelTest, AsyncQueueWhenBlocked) {
  constexpr EventCode kTestEventCode0 = 0xF0;
  // Set up expectations
  // clang-format off
  // Using HCI_Reset for testing.
  auto req_reset = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );
  auto rsp_resetstatus = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      Status::kSuccess, 0xFA, // status, num_hci_command_packets (250 can be sent)
      LowerBits(kReset), UpperBits(kReset)  // HCI_Reset opcode
      );
  auto rsp_bogocomplete = common::CreateStaticByteBuffer(
      kTestEventCode0,
      0x00 // parameter_total_size (no payload)
      );
  auto rsp_nocommandsavail = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (3 byte payload)
      Status::kSuccess, 0x00, // status, num_hci_command_packets (none can be sent)
      0x00, 0x00 // No associated opcode.
      );
  auto rsp_commandsavail = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (3 byte payload)
      Status::kSuccess, 0x01, // status, num_hci_command_packets (one can be sent)
      0x00, 0x00 // No associated opcode.
      );
  // clang-format on

  size_t transaction_count = 0u;

  test_device()->SetTransactionCallback(
      [&transaction_count]() { transaction_count++; },
      message_loop()->task_runner());

  test_device()->QueueCommandTransaction(
      CommandTransaction(req_reset, {&rsp_resetstatus, &rsp_bogocomplete}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  test_device()->SendCommandChannelPacket(rsp_nocommandsavail);

  RunUntilIdle();

  CommandChannel::TransactionId id;
  size_t cb_count = 0;
  auto cb = [&cb_count, &id, kTestEventCode0, this](
                CommandChannel::TransactionId callback_id,
                const EventPacket& event) {
    cb_count++;
    EXPECT_EQ(callback_id, id);
    if (cb_count == 1) {
      EXPECT_EQ(kCommandStatusEventCode, event.event_code());
      const auto params = event.view().payload<CommandStatusEventParams>();
      EXPECT_EQ(Status::kSuccess, params.status);
      EXPECT_EQ(kReset, params.command_opcode);
    } else {
      EXPECT_EQ(kTestEventCode0, event.event_code());
    }
  };

  auto packet = CommandPacket::New(kReset);
  id = cmd_channel()->SendCommand(
      std::move(packet), message_loop()->task_runner(), cb, kTestEventCode0);

  RunUntilIdle();

  ASSERT_NE(0u, id);
  ASSERT_EQ(0u, transaction_count);

  auto invalid_id = cmd_channel()->AddEventHandler(
      kTestEventCode0, [](const auto&) {}, message_loop()->task_runner());

  RunUntilIdle();

  ASSERT_EQ(0u, invalid_id);

  // Commands become available and the whole transaction finishes.
  test_device()->SendCommandChannelPacket(rsp_commandsavail);

  RunUntilIdle();

  ASSERT_EQ(1u, transaction_count);
  ASSERT_EQ(2u, cb_count);
}

// Tests:
//  - Events are routed to the event handler.
//  - Can't queue a command on the same event that is already in an event
//  handler.
TEST_F(HCI_CommandChannelTest, EventHandlerBasic) {
  constexpr EventCode kTestEventCode0 = 0xFE;
  constexpr EventCode kTestEventCode1 = 0xFF;
  auto cmd_status = common::CreateStaticByteBuffer(
      kCommandStatusEventCode, 0x04, 0x00, 0x01, 0x00, 0x00);
  auto cmd_complete = common::CreateStaticByteBuffer(kCommandCompleteEventCode,
                                                     0x03, 0x01, 0x00, 0x00);
  auto event0 = common::CreateStaticByteBuffer(kTestEventCode0, 0x00);
  auto event1 = common::CreateStaticByteBuffer(kTestEventCode1, 0x00);

  int event_count0 = 0;
  auto event_cb0 = [&event_count0, kTestEventCode0](const EventPacket& event) {
    event_count0++;
    EXPECT_EQ(kTestEventCode0, event.event_code());
  };

  int event_count1 = 0;
  auto event_cb1 = [&event_count1, kTestEventCode1,
                    this](const EventPacket& event) {
    event_count1++;
    EXPECT_EQ(kTestEventCode1, event.event_code());

    // The code below will send this event twice. Quit the message loop when we
    // get the second event.
    if (event_count1 == 2)
      message_loop()->PostQuitTask();
  };

  auto id0 = cmd_channel()->AddEventHandler(kTestEventCode0, event_cb0,
                                            message_loop()->task_runner());
  EXPECT_NE(0u, id0);

  // Cannot register a handler for the same event code more than once.
  auto id1 = cmd_channel()->AddEventHandler(kTestEventCode0, event_cb1,
                                            message_loop()->task_runner());
  EXPECT_EQ(0u, id1);

  // Add a handler for a different event code.
  id1 = cmd_channel()->AddEventHandler(kTestEventCode1, event_cb1,
                                       message_loop()->task_runner());
  EXPECT_NE(0u, id1);

  auto reset = CommandPacket::New(kReset);
  auto transaction_id = cmd_channel()->SendCommand(
      std::move(reset), message_loop()->task_runner(), [](auto, const auto&) {},
      kTestEventCode0);

  EXPECT_EQ(0u, transaction_id);

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());
  test_device()->SendCommandChannelPacket(cmd_status);
  test_device()->SendCommandChannelPacket(cmd_complete);
  test_device()->SendCommandChannelPacket(event1);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(cmd_complete);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(cmd_status);
  test_device()->SendCommandChannelPacket(event1);

  RunMessageLoop();

  EXPECT_EQ(3, event_count0);
  EXPECT_EQ(2, event_count1);

  event_count0 = 0;
  event_count1 = 0;

  // Remove the first event handler.
  cmd_channel()->RemoveEventHandler(id0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event1);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event0);
  test_device()->SendCommandChannelPacket(event1);

  RunMessageLoop();

  EXPECT_EQ(0, event_count0);
  EXPECT_EQ(2, event_count1);
}

// Tests:
//  - can't send a command that masks an event handler.
//  - can send a command without a callback.
TEST_F(HCI_CommandChannelTest, EventHandlerEventWhileTransactionPending) {
  // clang-format off
  // HCI_Reset
  auto req = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
      );

  auto req_complete = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x03,  // parameter_total_size (3 byte payload)
      0x01, // num_hci_command_packets (1 can be sent)
      LowerBits(kReset), UpperBits(kReset)  // HCI_Reset opcode
      );
  // clang-format on

  constexpr EventCode kTestEventCode = 0xFF;
  auto event = common::CreateStaticByteBuffer(kTestEventCode, 0x01, 0x00);

  // We will send the HCI_Reset command with kTestEventCode as the completion
  // event. The event handler we register below should only get invoked once and
  // after the pending transaction completes.
  test_device()->QueueCommandTransaction(
      CommandTransaction(req, {&req_complete, &event, &event}));
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  int event_count = 0;
  auto event_cb = [&event_count, kTestEventCode,
                   this](const EventPacket& event) {
    event_count++;
    EXPECT_EQ(kTestEventCode, event.event_code());
    EXPECT_EQ(1u, event.view().header().parameter_total_size);
    EXPECT_EQ(1u, event.view().payload_size());

    // We post this task to the end of the message queue so that the quit call
    // doesn't inherently guarantee that this callback gets invoked only once.
    if (event_count == 2) {
      message_loop()->PostQuitTask();
    }
  };

  cmd_channel()->AddEventHandler(kTestEventCode, event_cb,
                                 message_loop()->task_runner());

  auto reset = CommandPacket::New(kReset);
  CommandChannel::TransactionId id = cmd_channel()->SendCommand(
      std::move(reset), message_loop()->task_runner(), nullptr, kTestEventCode);
  EXPECT_EQ(0u, id);

  reset = CommandPacket::New(kReset);
  id = cmd_channel()->SendCommand(std::move(reset),
                                  message_loop()->task_runner(), nullptr);
  EXPECT_NE(0u, id);

  RunMessageLoop();

  EXPECT_EQ(2, event_count);
}

TEST_F(HCI_CommandChannelTest, LEMetaEventHandler) {
  constexpr EventCode kTestSubeventCode0 = 0xFE;
  constexpr EventCode kTestSubeventCode1 = 0xFF;
  auto le_meta_event_bytes0 = common::CreateStaticByteBuffer(
      hci::kLEMetaEventCode, 0x01, kTestSubeventCode0);
  auto le_meta_event_bytes1 = common::CreateStaticByteBuffer(
      hci::kLEMetaEventCode, 0x01, kTestSubeventCode1);

  int event_count0 = 0;
  auto event_cb0 = [&event_count0, kTestSubeventCode0,
                    this](const EventPacket& event) {
    event_count0++;
    EXPECT_EQ(hci::kLEMetaEventCode, event.event_code());
    EXPECT_EQ(kTestSubeventCode0,
              event.view().payload<LEMetaEventParams>().subevent_code);
    message_loop()->PostQuitTask();
  };

  int event_count1 = 0;
  auto event_cb1 = [&event_count1, kTestSubeventCode1,
                    this](const EventPacket& event) {
    event_count1++;
    EXPECT_EQ(hci::kLEMetaEventCode, event.event_code());
    EXPECT_EQ(kTestSubeventCode1,
              event.view().payload<LEMetaEventParams>().subevent_code);
    message_loop()->PostQuitTask();
  };

  auto id0 = cmd_channel()->AddLEMetaEventHandler(
      kTestSubeventCode0, event_cb0, message_loop()->task_runner());
  EXPECT_NE(0u, id0);

  // Cannot register a handler for the same event code more than once.
  auto id1 = cmd_channel()->AddLEMetaEventHandler(
      kTestSubeventCode0, event_cb0, message_loop()->task_runner());
  EXPECT_EQ(0u, id1);

  // Add a handle for a different event code.
  id1 = cmd_channel()->AddLEMetaEventHandler(kTestSubeventCode1, event_cb1,
                                             message_loop()->task_runner());
  EXPECT_NE(0u, id1);

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  test_device()->SendCommandChannelPacket(le_meta_event_bytes0);
  RunMessageLoop();
  EXPECT_EQ(1, event_count0);
  EXPECT_EQ(0, event_count1);

  test_device()->SendCommandChannelPacket(le_meta_event_bytes0);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(0, event_count1);

  test_device()->SendCommandChannelPacket(le_meta_event_bytes1);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(1, event_count1);

  // Remove the first event handler.
  cmd_channel()->RemoveEventHandler(id0);
  test_device()->SendCommandChannelPacket(le_meta_event_bytes0);
  test_device()->SendCommandChannelPacket(le_meta_event_bytes1);
  RunMessageLoop();
  EXPECT_EQ(2, event_count0);
  EXPECT_EQ(2, event_count1);
}

TEST_F(HCI_CommandChannelTest, EventHandlerIdsDontCollide) {
  // Add a LE Meta event handler and a event handler and make sure that IDs are
  // generated correctly across the two methods.
  EXPECT_EQ(1u, cmd_channel()->AddLEMetaEventHandler(
                    hci::kLEConnectionCompleteSubeventCode, [](const auto&) {},
                    message_loop()->task_runner()));
  EXPECT_EQ(2u, cmd_channel()->AddEventHandler(
                    hci::kDisconnectionCompleteEventCode, [](const auto&) {},
                    message_loop()->task_runner()));
}

// Tests:
//  - Can't register an event handler for CommandStatus or CommandComplete
TEST_F(HCI_CommandChannelTest, EventHandlerRestrictions) {
  auto id0 = cmd_channel()->AddEventHandler(hci::kCommandStatusEventCode,
                                            [](const auto&) {},
                                            message_loop()->task_runner());
  EXPECT_EQ(0u, id0);
  id0 = cmd_channel()->AddEventHandler(hci::kCommandCompleteEventCode,
                                       [](const auto&) {},
                                       message_loop()->task_runner());
  EXPECT_EQ(0u, id0);
}

TEST_F(HCI_CommandChannelTest, TransportClosedCallback) {
  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  bool closed_cb_called = false;
  auto closed_cb = [&closed_cb_called, this] {
    closed_cb_called = true;
    message_loop()->QuitNow();
  };
  transport()->SetTransportClosedCallback(closed_cb,
                                          message_loop()->task_runner());

  async::PostTask(message_loop()->async(),
                  [this] { test_device()->CloseCommandChannel(); });
  RunMessageLoop();
  EXPECT_TRUE(closed_cb_called);
}

// When a command times out:
//  - All the queued and pending commands are notified via callbacks.
//  - Shutdown happens.
TEST_F(HCI_CommandChannelTest, CommandTimeout) {
  auto req_reset = common::CreateStaticByteBuffer(
      LowerBits(kReset), UpperBits(kReset),  // HCI_Reset opcode
      0x00                                   // parameter_total_size
  );
  test_device()->QueueCommandTransaction(CommandTransaction(req_reset, {}));

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  // Set a short timeout for fast tests.
  cmd_channel()->set_command_timeout_ms(5);

  size_t cb_count = 0;
  CommandChannel::TransactionId id1, id2;
  auto cb = [&cb_count, &id1, &id2, this](
                CommandChannel::TransactionId callback_id,
                const EventPacket& event) {
    cb_count++;
    EXPECT_NE(0u, callback_id);
    EXPECT_TRUE(callback_id == id1 || callback_id == id2);
    EXPECT_EQ(kCommandStatusEventCode, event.event_code());
    const auto params = event.view().payload<CommandStatusEventParams>();
    EXPECT_EQ(Status::kUnspecifiedError, params.status);
    EXPECT_EQ(kReset, params.command_opcode);
    if (cb_count == 2) {
      message_loop()->QuitNow();
    }
  };

  auto packet = CommandPacket::New(kReset);
  id1 = cmd_channel()->SendCommand(std::move(packet),
                                   message_loop()->task_runner(), cb);
  EXPECT_NE(0u, id1);

  packet = CommandPacket::New(kReset);
  id2 = cmd_channel()->SendCommand(std::move(packet),
                                   message_loop()->task_runner(), cb);
  EXPECT_NE(0u, id2);

  RunMessageLoop();

  EXPECT_EQ(2u, cb_count);
}

}  // namespace
}  // namespace hci
}  // namespace btlib
