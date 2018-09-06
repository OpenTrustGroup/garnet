// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace btlib {
namespace hci {
namespace {

constexpr OpCode kTestOpCode = 0xFFFF;
constexpr OpCode kTestOpCode2 = 0xF00F;

using ::btlib::testing::CommandTransaction;

using TestingBase =
    ::btlib::testing::FakeControllerTest<::btlib::testing::TestController>;

class SequentialCommandRunnerTest : public TestingBase {
 public:
  SequentialCommandRunnerTest() = default;
  ~SequentialCommandRunnerTest() override = default;
};

using HCI_SequentialCommandRunnerTest = SequentialCommandRunnerTest;

TEST_F(HCI_SequentialCommandRunnerTest, SequentialCommandRunner) {
  // HCI command with custom opcode FFFF.
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);

  auto command_status_error_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      StatusCode::kHardwareFailure, 1, 0xFF, 0xFF);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kReserved0);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kSuccess);

  // Here we perform multiple test sequences where we queue up several  commands
  // in each sequence. We expect each sequence to terminate differently after
  // the following HCI transactions:
  //
  // Sequence 1 (HCI packets)
  //    -> Command; <- error status
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_status_error_bytes}));

  // Sequence 2 (HCI packets)
  //    -> Command; <- error complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_error_bytes}));

  // Sequence 3 (HCI packets)
  //    -> Command; <- success complete
  //    -> Command; <- error complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_error_bytes}));

  // Sequence 4 (HCI packets)
  //    -> Command; <- success complete
  //    -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  // Sequence 5 (HCI packets)
  //    -> Command; <- success complete
  //    -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  Status status;
  int status_cb_called = 0;
  auto status_cb = [&, this](Status cb_status) {
    status = cb_status;
    status_cb_called++;
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1 (test)
  SequentialCommandRunner cmd_runner(dispatcher(), transport());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(1, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());
  cb_called = 0;

  // Sequence 2 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(1, cb_called);
  EXPECT_EQ(2, status_cb_called);
  EXPECT_EQ(StatusCode::kReserved0, status.protocol_error());
  cb_called = 0;

  // Sequence 3 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(2, cb_called);
  EXPECT_EQ(3, status_cb_called);
  EXPECT_EQ(StatusCode::kReserved0, status.protocol_error());
  cb_called = 0;

  // Sequence 4 (test)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(2, cb_called);
  EXPECT_EQ(4, status_cb_called);
  EXPECT_TRUE(status);
  cb_called = 0;
  status_cb_called = 0;

  // Sequence 5 (test) (no callback passed to QueueCommand)
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode));
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode));

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());
  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_TRUE(status);
}

TEST_F(HCI_SequentialCommandRunnerTest, SequentialCommandRunnerCancel) {
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kHardwareFailure);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      1, 0xFF, 0xFF, StatusCode::kSuccess);

  // Sequence 1
  //   -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  // Sequence 2
  //   -> Command; <- success complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  // Sequence 3
  //   -> Command; <- success complete
  //   -> Command; <- error complete
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_error_bytes}));

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());

  Status status;
  int status_cb_called = 0;
  auto status_cb = [&, this](Status cb_status) {
    status = cb_status;
    status_cb_called++;
  };

  int cb_called = 0;
  auto cb = [&](const EventPacket& event) { cb_called++; };

  // Sequence 1: Sequence will be cancelled after the first command.
  SequentialCommandRunner cmd_runner(dispatcher(), transport());
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  // Call RunCommands() and right away post a task to cancel the sequence. The
  // first command will go out but no successive packets should be sent.
  // status callbacks should be invoked
  // the command callback for the first command should run but no others.
  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());
  cmd_runner.Cancel();

  // Since |status_cb| is expected to not get called (which would normally quit
  // the message loop) - we run until we reach a steady-state waiting.
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  EXPECT_EQ(1, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_EQ(common::HostError::kCanceled, status.error());
  cb_called = 0;
  status_cb_called = 0;
  status = Status();

  // Sequence 2: Sequence will be cancelled after first command. This tests
  // canceling a sequence from a CommandCompleteCallback.
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          [&](const EventPacket& event) {
                            bt_log(SPEW, "hci-test", "callback called");
                            cmd_runner.Cancel();
                            EXPECT_TRUE(cmd_runner.IsReady());
                            EXPECT_FALSE(cmd_runner.HasQueuedCommands());
                          });
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  // |status_cb| is expected to get called with kCanceled
  RunLoopUntilIdle();
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  EXPECT_EQ(0, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_EQ(common::HostError::kCanceled, status.error());

  // Sequence 3: Sequence will be cancelled after first command and immediately
  // followed by a second command which will fail. This tests canceling a
  // sequence and initiating a new one from a CommandCompleteCallback.
  cmd_runner.QueueCommand(
      CommandPacket::New(kTestOpCode), [&](const EventPacket& event) {
        cmd_runner.Cancel();
        EXPECT_TRUE(cmd_runner.IsReady());
        EXPECT_FALSE(cmd_runner.HasQueuedCommands());

        EXPECT_EQ(2, status_cb_called);
        EXPECT_EQ(common::HostError::kCanceled, status.error());

        // Queue multiple commands (only one will execute since TestController
        // will send back an error status.
        cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb);
        cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                                cb);  // <-- Should not run
        cmd_runner.RunCommands(status_cb);
      });
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // <-- Should not run
  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_TRUE(cmd_runner.HasQueuedCommands());

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  RunLoopUntilIdle();

  EXPECT_TRUE(cmd_runner.IsReady());
  EXPECT_FALSE(cmd_runner.HasQueuedCommands());

  // The cb queued from inside the first callback should have been called.
  EXPECT_EQ(1, cb_called);
  // The result callback should have been called with the failure result.
  EXPECT_EQ(3, status_cb_called);
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());
}

TEST_F(HCI_SequentialCommandRunnerTest, ParallelCommands) {
  // Need to signal to the queue that we can run more than one command at once.
  auto command_status_queue_increase = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      StatusCode::kSuccess, 250, 0x00, 0x00);
  // HCI command with custom opcode FFFF.
  auto command_bytes = common::CreateStaticByteBuffer(0xFF, 0xFF, 0x00);
  auto command_status_error_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      StatusCode::kHardwareFailure, 2, 0xFF, 0xFF);

  auto command_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      2, 0xFF, 0xFF, StatusCode::kReserved0);

  auto command_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      2, 0xFF, 0xFF, StatusCode::kSuccess);

  // HCI command with custom opcode F00F.
  auto command2_bytes = common::CreateStaticByteBuffer(0x0F, 0xF0, 0x00);
  auto command2_status_error_bytes = common::CreateStaticByteBuffer(
      kCommandStatusEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      StatusCode::kHardwareFailure, 2, 0x0F, 0xF0);

  auto command2_cmpl_error_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      2, 0x0F, 0xF0, StatusCode::kReserved0);

  auto command2_cmpl_success_bytes = common::CreateStaticByteBuffer(
      kCommandCompleteEventCode,
      0x04,  // parameter_total_size (4 byte payload)
      2, 0x0F, 0xF0, StatusCode::kSuccess);

  test_device()->StartCmdChannel(test_cmd_chan());
  test_device()->StartAclChannel(test_acl_chan());
  test_device()->SendCommandChannelPacket(command_status_queue_increase);

  // Parallel commands should all run before commands that require success.
  // command and command2 are answered in opposite order because they should be
  // sent simultaneously.
  test_device()->QueueCommandTransaction(CommandTransaction(command_bytes, {}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command2_bytes, {}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command_bytes, {&command_cmpl_success_bytes}));

  int cb_called = 0;
  auto cb = [&](const auto&) { cb_called++; };

  int status_cb_called = 0;
  Status status(common::HostError::kFailed);
  auto status_cb = [&](Status cb_status) {
    status = cb_status;
    status_cb_called++;
  };

  SequentialCommandRunner cmd_runner(dispatcher(), transport());

  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb, false);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode2), cb, false);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          [&](const auto&) {
                            EXPECT_EQ(2, cb_called);
                            cb_called++;
                          },
                          true);
  // We can also queue to the end of the queue without the last one being a
  // wait.
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb, false);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb, false);
  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  RunLoopUntilIdle();
  // The first two commands should have been sent but no responses are back yet.
  EXPECT_EQ(0, cb_called);

  // It should not matter if they get answered in opposite order.
  test_device()->SendCommandChannelPacket(command2_cmpl_success_bytes);
  test_device()->SendCommandChannelPacket(command_cmpl_success_bytes);
  RunLoopUntilIdle();

  EXPECT_EQ(5, cb_called);
  EXPECT_TRUE(status);
  EXPECT_EQ(1, status_cb_called);
  cb_called = 0;
  status_cb_called = 0;

  // If any simultaneous commands fail, the sequence fails and the command
  // sequence is terminated.
  test_device()->QueueCommandTransaction(CommandTransaction(command_bytes, {}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(command2_bytes, {}));

  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode), cb, false);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode2), cb, false);
  cmd_runner.QueueCommand(CommandPacket::New(kTestOpCode),
                          cb);  // shouldn't run

  cmd_runner.RunCommands(status_cb);
  EXPECT_FALSE(cmd_runner.IsReady());

  RunLoopUntilIdle();
  // The first two commands should have been sent but no responses are back yet.
  EXPECT_EQ(0, cb_called);

  test_device()->SendCommandChannelPacket(command_status_error_bytes);
  test_device()->SendCommandChannelPacket(command2_cmpl_success_bytes);
  RunLoopUntilIdle();

  EXPECT_EQ(2, cb_called);
  EXPECT_EQ(1, status_cb_called);
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());
}

}  // namespace
}  // namespace hci
}  // namespace btlib
