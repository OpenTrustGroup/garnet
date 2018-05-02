// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"

#include <array>
#include <cstdint>

#include <endian.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

using btlib::common::ContainersEqual;
using btlib::common::StaticByteBuffer;

namespace btlib {
namespace hci {
namespace test {
namespace {

constexpr OpCode kTestOpCode = 0x07FF;
constexpr EventCode kTestEventCode = 0xFF;

struct TestPayload {
  uint8_t foo;
} __PACKED;

TEST(HCI_PacketTest, CommandPacket) {
  constexpr size_t kPayloadSize = sizeof(TestPayload);
  auto packet = CommandPacket::New(kTestOpCode, kPayloadSize);

  EXPECT_EQ(kTestOpCode, packet->opcode());
  EXPECT_EQ(kPayloadSize, packet->view().payload_size());

  packet->mutable_view()->mutable_payload<TestPayload>()->foo = 127;

  // clang-format off

  auto kExpected = common::CreateStaticByteBuffer(
      0xFF, 0x07,  // opcode
      0x01,        // parameter_total_size
      0x7F         // foo
  );

  // clang-format on

  EXPECT_TRUE(ContainersEqual(kExpected, packet->view().data()));
}

TEST(HCI_PacketTest, EventPacket) {
  constexpr size_t kPayloadSize = sizeof(TestPayload);
  auto packet = EventPacket::New(kPayloadSize);

  // clang-format off

  auto bytes = common::CreateStaticByteBuffer(
      0xFF,  // event code
      0x01,  // parameter_total_size
      0x7F   // foo
  );
  packet->mutable_view()->mutable_data().Write(bytes);
  packet->InitializeFromBuffer();

  // clang-format on

  EXPECT_EQ(kTestEventCode, packet->event_code());
  EXPECT_EQ(kPayloadSize, packet->view().payload_size());
  EXPECT_EQ(127, packet->view().payload<TestPayload>().foo);
}

TEST(HCI_PacketTest, EventPacketReturnParams) {
  // clang-format off

  auto correct_size_bad_event_code = common::CreateStaticByteBuffer(
      // Event header
      0xFF, 0x04,  // (event_code is not CommandComplete)

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07,

      // Return parameters
      0x7F);
  auto cmd_complete_small_payload = common::CreateStaticByteBuffer(
      // Event header
      0x0E, 0x03,

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07);
  auto valid = common::CreateStaticByteBuffer(
      // Event header
      0x0E, 0x04,

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07,

      // Return parameters
      0x7F);

  // clang-format on

  // Allocate a large enough packet which we'll reuse for the 3 payloads.
  auto packet = EventPacket::New(valid.size());

  // If the event code or the payload size don't match, then return_params()
  // should return nullptr.
  packet->mutable_view()->mutable_data().Write(correct_size_bad_event_code);
  packet->InitializeFromBuffer();
  EXPECT_EQ(nullptr, packet->return_params<TestPayload>());

  packet->mutable_view()->mutable_data().Write(cmd_complete_small_payload);
  packet->InitializeFromBuffer();
  EXPECT_EQ(nullptr, packet->return_params<TestPayload>());

  // Reset packet size to the original so that |valid| can fit.
  packet->mutable_view()->Resize(valid.size());

  // Valid case
  packet->mutable_view()->mutable_data().Write(valid);
  packet->InitializeFromBuffer();
  ASSERT_NE(nullptr, packet->return_params<TestPayload>());
  EXPECT_EQ(127, packet->return_params<TestPayload>()->foo);
}

TEST(HCI_PacketTest, EventPacketStatus) {
  // clang-format off
  auto evt = common::CreateStaticByteBuffer(
      // Event header
      0x05, 0x04,  // (event_code is DisconnectionComplete)

      // Disconnection Complete event parameters
      0x03,        // status: hardware failure
      0x01, 0x00,  // handle: 0x0001
      0x16         // reason: terminated by local host
  );
  // clang-format on

  auto packet = EventPacket::New(evt.size());
  packet->mutable_view()->mutable_data().Write(evt);
  packet->InitializeFromBuffer();

  Status status = packet->ToStatus();
  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());
}

TEST(HCI_PacketTest, CommandCompleteEventStatus) {
  // clang-format off
  auto evt = common::CreateStaticByteBuffer(
      // Event header
      0x0E, 0x04,  // (event code is CommandComplete)

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07,

      // Return parameters (status: hardware failure)
      0x03);
  // clang-format on

  auto packet = EventPacket::New(evt.size());
  packet->mutable_view()->mutable_data().Write(evt);
  packet->InitializeFromBuffer();

  Status status = packet->ToStatus();
  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(StatusCode::kHardwareFailure, status.protocol_error());
}

TEST(HCI_PacketTest, EventPacketMalformed) {
  // clang-format off
  auto evt = common::CreateStaticByteBuffer(
      // Event header
      0x05, 0x03,  // (event_code is DisconnectionComplete)

      // Disconnection Complete event parameters
      0x03,        // status: hardware failure
      0x01, 0x00   // handle: 0x0001
      // event is one byte too short
  );
  // clang-format on

  auto packet = EventPacket::New(evt.size());
  packet->mutable_view()->mutable_data().Write(evt);
  packet->InitializeFromBuffer();

  Status status = packet->ToStatus();
  EXPECT_FALSE(status.is_protocol_error());
  EXPECT_EQ(common::HostError::kPacketMalformed, status.error());
}

TEST(HCI_PacketTest, LEEventParams) {
  // clang-format off

  auto correct_size_bad_event_code = common::CreateStaticByteBuffer(
      // Event header
      0xFF, 0x02,  // (event_code is not LEMetaEventCode)

      // Subevent code
      0xFF,

      // Subevent payload
      0x7F);
  auto payload_too_small = common::CreateStaticByteBuffer(
      0x3E, 0x01,

      // Subevent code
      0xFF);
  auto valid = common::CreateStaticByteBuffer(
      // Event header
      0x3E, 0x02,

      // Subevent code
      0xFF,

      // Subevent payload
      0x7F);

  // clang-format on

  auto packet = EventPacket::New(valid.size());

  // If the event code or the payload size don't match, then return_params()
  // should return nullptr.
  packet->mutable_view()->mutable_data().Write(correct_size_bad_event_code);
  packet->InitializeFromBuffer();
  EXPECT_EQ(nullptr, packet->le_event_params<TestPayload>());

  packet->mutable_view()->mutable_data().Write(payload_too_small);
  packet->InitializeFromBuffer();
  EXPECT_EQ(nullptr, packet->le_event_params<TestPayload>());

  // Valid case
  packet->mutable_view()->Resize(valid.size());
  packet->mutable_view()->mutable_data().Write(valid);
  packet->InitializeFromBuffer();

  EXPECT_NE(nullptr, packet->le_event_params<TestPayload>());
  EXPECT_EQ(127, packet->le_event_params<TestPayload>()->foo);
}

TEST(HCI_PacketTest, ACLDataPacketFromFields) {
  constexpr size_t kLargeDataLength = 10;
  constexpr size_t kSmallDataLength = 1;

  auto packet = ACLDataPacket::New(
      0x007F, ACLPacketBoundaryFlag::kContinuingFragment,
      ACLBroadcastFlag::kActiveSlaveBroadcast, kSmallDataLength);
  packet->mutable_view()->mutable_payload_data().Fill(0);

  // First 12-bits: 0x07F
  // Upper 4-bits: 0b0101
  EXPECT_TRUE(
      ContainersEqual(packet->view().data(),
                      std::array<uint8_t, 5>{{0x7F, 0x50, 0x01, 0x00, 0x00}}));

  packet = ACLDataPacket::New(0x0FFF, ACLPacketBoundaryFlag::kCompletePDU,
                              ACLBroadcastFlag::kActiveSlaveBroadcast,
                              kSmallDataLength);
  packet->mutable_view()->mutable_payload_data().Fill(0);

  // First 12-bits: 0xFFF
  // Upper 4-bits: 0b0111
  EXPECT_TRUE(
      ContainersEqual(packet->view().data(),
                      std::array<uint8_t, 5>{{0xFF, 0x7F, 0x01, 0x00, 0x00}}));

  packet =
      ACLDataPacket::New(0x0FFF, ACLPacketBoundaryFlag::kFirstNonFlushable,
                         ACLBroadcastFlag::kPointToPoint, kLargeDataLength);
  packet->mutable_view()->mutable_payload_data().Fill(0);

  // First 12-bits: 0xFFF
  // Upper 4-bits: 0b0000
  EXPECT_TRUE(ContainersEqual(
      packet->view().data(),
      std::array<uint8_t, 14>{{0xFF, 0x0F, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00}}));
}

TEST(HCI_PacketTest, ACLDataPacketFromBuffer) {
  constexpr size_t kLargeDataLength = 256;
  constexpr size_t kSmallDataLength = 1;

  // The same test cases as ACLDataPacketFromFields test above but in the
  // opposite direction.

  // First 12-bits: 0x07F
  // Upper 4-bits: 0b0101
  auto bytes = common::CreateStaticByteBuffer(0x7F, 0x50, 0x01, 0x00, 0x00);
  auto packet = ACLDataPacket::New(kSmallDataLength);
  packet->mutable_view()->mutable_data().Write(bytes);
  packet->InitializeFromBuffer();

  EXPECT_EQ(0x007F, packet->connection_handle());
  EXPECT_EQ(ACLPacketBoundaryFlag::kContinuingFragment,
            packet->packet_boundary_flag());
  EXPECT_EQ(ACLBroadcastFlag::kActiveSlaveBroadcast, packet->broadcast_flag());
  EXPECT_EQ(kSmallDataLength, packet->view().payload_size());

  // First 12-bits: 0xFFF
  // Upper 4-bits: 0b0111
  bytes = common::CreateStaticByteBuffer(0xFF, 0x7F, 0x01, 0x00, 0x00);
  packet->mutable_view()->mutable_data().Write(bytes);
  packet->InitializeFromBuffer();

  EXPECT_EQ(0x0FFF, packet->connection_handle());
  EXPECT_EQ(ACLPacketBoundaryFlag::kCompletePDU,
            packet->packet_boundary_flag());
  EXPECT_EQ(ACLBroadcastFlag::kActiveSlaveBroadcast, packet->broadcast_flag());
  EXPECT_EQ(kSmallDataLength, packet->view().payload_size());

  packet = ACLDataPacket::New(kLargeDataLength);
  packet->mutable_view()->mutable_data().Write(
      common::CreateStaticByteBuffer(0xFF, 0x0F, 0x00, 0x01));
  packet->InitializeFromBuffer();

  EXPECT_EQ(0x0FFF, packet->connection_handle());
  EXPECT_EQ(ACLPacketBoundaryFlag::kFirstNonFlushable,
            packet->packet_boundary_flag());
  EXPECT_EQ(ACLBroadcastFlag::kPointToPoint, packet->broadcast_flag());
  EXPECT_EQ(kLargeDataLength, packet->view().payload_size());
}

}  // namespace
}  // namespace test
}  // namespace hci
}  // namespace btlib
