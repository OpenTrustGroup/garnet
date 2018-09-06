// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "le_signaling_channel.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"

#include "channel.h"
#include "logical_link.h"

namespace btlib {
namespace l2cap {
namespace internal {

LESignalingChannel::LESignalingChannel(fbl::RefPtr<Channel> chan,
                                       hci::Connection::Role role)
    : SignalingChannel(std::move(chan), role) {
  set_mtu(kMinLEMTU);
}

bool LESignalingChannel::SendRequest(CommandCode req_code,
                                     const common::ByteBuffer& payload,
                                     ResponseHandler cb) {
  // TODO(NET-1093): Reuse BrEdrSignalingChannel's implementation.
  bt_log(WARN, "l2cap-le", "sig: SendRequest not implemented yet");
  return false;
}

void LESignalingChannel::ServeRequest(CommandCode req_code,
                                      RequestDelegate cb) {
  bt_log(WARN, "l2cap-le", "sig: ServeRequest not implemented yet");
}

void LESignalingChannel::OnConnParamUpdateReceived(
    const SignalingPacket& packet) {
  // Only a LE slave can send this command. "If an LE slave Host receives a
  // Connection Parameter Update Request packet it shall respond with a Command
  // Reject Packet [...]" (v5.0, Vol 3, Part A, Section 4.20).
  if (role() == hci::Connection::Role::kSlave) {
    bt_log(TRACE, "l2cap-le",
           "sig: rejecting conn. param. update request from master");
    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
    return;
  }

  if (packet.payload_size() !=
      sizeof(ConnectionParameterUpdateRequestPayload)) {
    bt_log(TRACE, "l2cap-le", "sig: malformed request received");
    SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                      common::BufferView());
    return;
  }

  const auto& payload =
      packet.payload<ConnectionParameterUpdateRequestPayload>();

  // Reject the connection parameters if they are outside the ranges allowed by
  // the HCI specification (see HCI_LE_Connection_Update command - v5.0, Vol 2,
  // Part E, Section 7.8.18).
  bool reject = false;
  hci::LEPreferredConnectionParameters params(
      le16toh(payload.interval_min), le16toh(payload.interval_max),
      le16toh(payload.slave_latency), le16toh(payload.timeout_multiplier));

  if (params.min_interval() > params.max_interval()) {
    bt_log(TRACE, "l2cap-le", "sig: conn. min interval larger than max");
    reject = true;
  } else if (params.min_interval() < hci::kLEConnectionIntervalMin) {
    bt_log(TRACE, "l2cap-le",
           "sig: conn. min interval outside allowed range: %#.4x",
           params.min_interval());
    reject = true;
  } else if (params.max_interval() > hci::kLEConnectionIntervalMax) {
    bt_log(TRACE, "l2cap-le",
           "sig: conn. max interval outside allowed range: %#.4x",
           params.max_interval());
    reject = true;
  } else if (params.max_latency() > hci::kLEConnectionLatencyMax) {
    bt_log(TRACE, "l2cap-le", "sig: conn. slave latency too large: %#.4x",
           params.max_latency());
    reject = true;
  } else if (params.supervision_timeout() <
                 hci::kLEConnectionSupervisionTimeoutMin ||
             params.supervision_timeout() >
                 hci::kLEConnectionSupervisionTimeoutMax) {
    bt_log(TRACE, "l2cap-le",
           "sig: conn supv. timeout outside allowed range: %#.4x",
           params.supervision_timeout());
    reject = true;
  }

  ConnectionParameterUpdateResult result =
      reject ? ConnectionParameterUpdateResult::kRejected
             : ConnectionParameterUpdateResult::kAccepted;
  ConnectionParameterUpdateResponsePayload rsp;
  rsp.result = static_cast<ConnectionParameterUpdateResult>(htole16(result));
  SendPacket(kConnectionParameterUpdateResponse, packet.header().id,
             common::BufferView(&rsp, sizeof(rsp)));

  if (!reject && dispatcher_) {
    async::PostTask(dispatcher_, [cb = conn_param_update_cb_.share(),
                                  params = std::move(params)]() mutable {
      cb(std::move(params));
    });
  }
}

void LESignalingChannel::DecodeRxUnit(const SDU& sdu,
                                      const SignalingPacketHandler& cb) {
  // "[O]nly one command per C-frame shall be sent over [the LE] Fixed Channel"
  // (v5.0, Vol 3, Part A, Section 4).
  if (sdu.length() < sizeof(CommandHeader)) {
    bt_log(TRACE, "l2cap-le", "sig: dropped malformed LE signaling packet");
    return;
  }

  SDU::Reader reader(&sdu);

  auto process_sdu_as_packet = [this, &cb](const auto& data) {
    SignalingPacket packet(&data);

    uint16_t expected_payload_length = le16toh(packet.header().length);
    if (expected_payload_length != data.size() - sizeof(CommandHeader)) {
      bt_log(TRACE, "l2cap-le",
             "sig: packet size mismatch (expected: %zu, recv: %zu); drop",
             expected_payload_length, data.size() - sizeof(CommandHeader));
      SendCommandReject(packet.header().id, RejectReason::kNotUnderstood,
                        common::BufferView());
      return;
    }

    cb(SignalingPacket(&data, expected_payload_length));
  };

  // Performing a single read for the entire length of an SDU can never fail.
  ZX_ASSERT(reader.ReadNext(sdu.length(), process_sdu_as_packet));
}

bool LESignalingChannel::HandlePacket(const SignalingPacket& packet) {
  switch (packet.header().code) {
    case kConnectionParameterUpdateRequest:
      OnConnParamUpdateReceived(packet);
      return true;
    default:
      bt_log(TRACE, "l2cap-le", "sig: unsupported code %#.2x",
             packet.header().code);
      break;
  }

  return false;
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
