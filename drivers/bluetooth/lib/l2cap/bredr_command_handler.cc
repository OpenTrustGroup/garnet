// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/l2cap/bredr_command_handler.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/packet_view.h"

namespace btlib {
namespace l2cap {
namespace internal {

using common::BufferView;
using common::ByteBuffer;

BrEdrCommandHandler::Responder::Responder(
    SignalingChannel::Responder* sig_responder, ChannelId local_cid,
    ChannelId remote_cid)
    : sig_responder_(sig_responder),
      local_cid_(local_cid),
      remote_cid_(remote_cid) {}

void BrEdrCommandHandler::Responder::RejectNotUnderstood() {
  sig_responder_->RejectNotUnderstood();
}

void BrEdrCommandHandler::Responder::RejectInvalidChannelId() {
  sig_responder_->RejectInvalidChannelId(local_cid(), remote_cid());
}

BrEdrCommandHandler::ConnectionResponder::ConnectionResponder(
    SignalingChannel::Responder* sig_responder, ChannelId remote_cid)
    : Responder(sig_responder, kInvalidChannelId, remote_cid) {}

void BrEdrCommandHandler::ConnectionResponder::Send(ChannelId local_cid,
                                                    ConnectionResult result,
                                                    ConnectionStatus status) {
  ConnectionResponsePayload conn_rsp = {
      htole16(local_cid), htole16(remote_cid()),
      static_cast<ConnectionResult>(htole16(result)),
      static_cast<ConnectionStatus>(htole16(status))};
  sig_responder_->Send(BufferView(&conn_rsp, sizeof(conn_rsp)));
}

BrEdrCommandHandler::ConfigurationResponder::ConfigurationResponder(
    SignalingChannel::Responder* sig_responder, ChannelId local_cid)
    : Responder(sig_responder, local_cid) {}

void BrEdrCommandHandler::ConfigurationResponder::Send(
    ChannelId remote_cid, uint16_t flags, ConfigurationResult result,
    const ByteBuffer& data) {
  common::DynamicByteBuffer config_rsp_buf(
      sizeof(ConfigurationResponsePayload) + data.size());
  common::MutablePacketView<ConfigurationResponsePayload> config_rsp(
      &config_rsp_buf, data.size());
  config_rsp.mutable_header()->src_cid = htole16(remote_cid);
  config_rsp.mutable_header()->flags = htole16(flags);
  config_rsp.mutable_header()->result =
      static_cast<ConfigurationResult>(htole16(result));
  config_rsp.mutable_payload_data().Write(data);
  sig_responder_->Send(config_rsp.data());
}

BrEdrCommandHandler::DisconnectionResponder::DisconnectionResponder(
    SignalingChannel::Responder* sig_responder, ChannelId local_cid,
    ChannelId remote_cid)
    : Responder(sig_responder, local_cid, remote_cid) {}

void BrEdrCommandHandler::DisconnectionResponder::Send() {
  DisconnectionResponsePayload discon_rsp = {htole16(local_cid()),
                                             htole16(remote_cid())};
  sig_responder_->Send(BufferView(&discon_rsp, sizeof(discon_rsp)));
}

BrEdrCommandHandler::InformationResponder::InformationResponder(
    SignalingChannel::Responder* sig_responder, InformationType type)
    : Responder(sig_responder), type_(type) {}

void BrEdrCommandHandler::InformationResponder::SendNotSupported() {
  Send(InformationResult::kNotSupported, BufferView());
}

void BrEdrCommandHandler::InformationResponder::SendConnectionlessMtu(
    uint16_t mtu) {
  mtu = htole16(mtu);
  Send(InformationResult::kSuccess, BufferView(&mtu, sizeof(mtu)));
}

void BrEdrCommandHandler::InformationResponder::SendExtendedFeaturesSupported(
    uint32_t mask) {
  mask = htole32(mask);
  Send(InformationResult::kSuccess, BufferView(&mask, sizeof(mask)));
}

void BrEdrCommandHandler::InformationResponder::SendFixedChannelsSupported(
    uint64_t mask) {
  mask = htole64(mask);
  Send(InformationResult::kSuccess, BufferView(&mask, sizeof(mask)));
}

void BrEdrCommandHandler::InformationResponder::Send(InformationResult result,
                                                     const ByteBuffer& data) {
  constexpr size_t kMaxPayloadLength =
      sizeof(InformationResponsePayload) + sizeof(uint64_t);
  common::StaticByteBuffer<kMaxPayloadLength> info_rsp_buf;
  common::MutablePacketView<InformationResponsePayload> info_rsp_view(
      &info_rsp_buf, data.size());

  info_rsp_view.mutable_header()->type =
      static_cast<InformationType>(htole16(type_));
  info_rsp_view.mutable_header()->result =
      static_cast<InformationResult>(htole16(result));
  info_rsp_view.mutable_payload_data().Write(data);
  sig_responder_->Send(info_rsp_view.data());
}

BrEdrCommandHandler::BrEdrCommandHandler(SignalingChannelInterface* sig)
    : sig_(sig) {
  FXL_DCHECK(sig_);
}

bool BrEdrCommandHandler::SendConnectionRequest(uint16_t psm,
                                                ChannelId local_cid,
                                                ConnectionResponseCallback cb) {
  auto on_conn_rsp = [cb = std::move(cb), this](Status status,
                                                const ByteBuffer& rsp_payload) {
    ConnectionResponse rsp;
    rsp.status_ = status;
    if (status == Status::kReject) {
      if (!ParseReject(rsp_payload, &rsp)) {
        return false;
      }
      return cb(rsp);
    }

    if (rsp_payload.size() != sizeof(ConnectionResponsePayload)) {
      FXL_VLOG(1)
          << "l2cap: BR/EDR cmd: ignoring malformed Connection Response, size "
          << rsp_payload.size();
      return false;
    }

    auto& conn_rsp_payload = rsp_payload.As<ConnectionResponsePayload>();
    rsp.remote_cid_ = letoh16(conn_rsp_payload.dst_cid);
    rsp.local_cid_ = letoh16(conn_rsp_payload.src_cid);
    rsp.result_ =
        static_cast<ConnectionResult>(letoh16(conn_rsp_payload.result));
    rsp.conn_status_ =
        static_cast<ConnectionStatus>(letoh16(conn_rsp_payload.status));
    return cb(rsp);
  };

  ConnectionRequestPayload payload = {htole16(psm), htole16(local_cid)};
  return sig_->SendRequest(kConnectionRequest,
                           BufferView(&payload, sizeof(payload)),
                           std::move(on_conn_rsp));
}

bool BrEdrCommandHandler::SendConfigurationRequest(
    ChannelId remote_cid, uint16_t flags, const ByteBuffer& options,
    ConfigurationResponseCallback cb) {
  auto on_config_rsp = [cb = std::move(cb), this](
                           Status status, const ByteBuffer& rsp_payload) {
    ConfigurationResponse rsp;
    rsp.status_ = status;
    if (status == Status::kReject) {
      if (!ParseReject(rsp_payload, &rsp)) {
        return false;
      }
      return cb(rsp);
    }

    if (rsp_payload.size() < sizeof(ConfigurationResponsePayload)) {
      FXL_VLOG(1) << "l2cap: BR/EDR cmd: ignoring malformed Configuration "
                     "Response, size "
                  << rsp_payload.size();
      return false;
    }

    common::PacketView<ConfigurationResponsePayload> config_rsp(
        &rsp_payload,
        rsp_payload.size() - sizeof(ConfigurationResponsePayload));
    rsp.remote_cid_ = letoh16(config_rsp.header().src_cid);
    rsp.flags_ = letoh16(config_rsp.header().flags);
    rsp.result_ =
        static_cast<ConfigurationResult>(letoh16(config_rsp.header().result));
    rsp.options_ = config_rsp.payload_data().view();
    return cb(rsp);
  };

  common::DynamicByteBuffer config_req_buf(sizeof(ConfigurationRequestPayload) +
                                           options.size());
  common::MutablePacketView<ConfigurationRequestPayload> config_req(
      &config_req_buf, options.size());
  config_req.mutable_header()->dst_cid = htole16(remote_cid);
  config_req.mutable_header()->flags = htole16(flags);
  config_req.mutable_payload_data().Write(options);
  return sig_->SendRequest(kConfigurationRequest, config_req_buf,
                           std::move(on_config_rsp));
}

bool BrEdrCommandHandler::SendDisconnectionRequest(
    ChannelId remote_cid, ChannelId local_cid,
    DisconnectionResponseCallback cb) {
  auto on_discon_rsp = [cb = std::move(cb), this](
                           Status status, const ByteBuffer& rsp_payload) {
    DisconnectionResponse rsp;
    rsp.status_ = status;
    if (status == Status::kReject) {
      if (!ParseReject(rsp_payload, &rsp)) {
        return false;
      }
      return cb(rsp);
    }

    if (rsp_payload.size() != sizeof(DisconnectionResponsePayload)) {
      FXL_VLOG(1) << "l2cap: BR/EDR cmd: ignoring malformed Disconnection "
                     "Response, size "
                  << rsp_payload.size();
      return false;
    }

    auto& disconn_rsp_payload = rsp_payload.As<DisconnectionResponsePayload>();
    rsp.local_cid_ = letoh16(disconn_rsp_payload.src_cid);
    rsp.remote_cid_ = letoh16(disconn_rsp_payload.dst_cid);
    return cb(rsp);
  };

  DisconnectionRequestPayload payload = {htole16(remote_cid),
                                         htole16(local_cid)};
  return sig_->SendRequest(kDisconnectionRequest,
                           BufferView(&payload, sizeof(payload)),
                           std::move(on_discon_rsp));
}

bool BrEdrCommandHandler::SendInformationRequest(
    InformationType type, InformationResponseCallback cb) {
  // TODO(NET-1135): Implement requesting remote features and fixed channels
  FXL_LOG(ERROR) << "l2cap: BR/EDR cmd: Information Request not sent";
  return false;
}

void BrEdrCommandHandler::ServeConnectionRequest(ConnectionRequestCallback cb) {
  // TODO(NET-1135): Serve inbound channels
  FXL_LOG(ERROR) << "l2cap: BR/EDR cmd: Connection Request not handled";
}

void BrEdrCommandHandler::ServeConfigurationRequest(
    ConfigurationRequestCallback cb) {
  auto on_config_req = [this, cb = std::move(cb)](
                           const ByteBuffer& request_payload,
                           SignalingChannel::Responder* sig_responder) {
    if (request_payload.size() < sizeof(ConfigurationRequestPayload)) {
      FXL_VLOG(1) << "l2cap: BR/EDR cmd: rejecting malformed Configuration "
                     "Request, size "
                  << request_payload.size();
      sig_responder->RejectNotUnderstood();
      return;
    }

    common::PacketView<ConfigurationRequestPayload> config_req(
        &request_payload,
        request_payload.size() - sizeof(ConfigurationRequestPayload));
    const auto local_cid =
        static_cast<ChannelId>(letoh16(config_req.header().dst_cid));
    const uint16_t flags = letoh16(config_req.header().flags);
    ConfigurationResponder responder(sig_responder, local_cid);
    cb(local_cid, flags, config_req.payload_data(), &responder);
  };

  sig_->ServeRequest(kConfigurationRequest, std::move(on_config_req));
}

void BrEdrCommandHandler::ServeDisconnectionRequest(
    DisconnectionRequestCallback cb) {
  auto on_discon_req = [this, cb = std::move(cb)](
                           const ByteBuffer& request_payload,
                           SignalingChannel::Responder* sig_responder) {
    if (request_payload.size() != sizeof(DisconnectionRequestPayload)) {
      FXL_VLOG(1) << "l2cap: BR/EDR cmd: rejecting malformed Disconnection "
                     "Request, size "
                  << request_payload.size();
      sig_responder->RejectNotUnderstood();
      return;
    }

    const auto& discon_req = request_payload.As<DisconnectionRequestPayload>();
    const ChannelId local_cid = letoh16(discon_req.dst_cid);
    const ChannelId remote_cid = letoh16(discon_req.src_cid);
    DisconnectionResponder responder(sig_responder, local_cid, remote_cid);
    cb(local_cid, remote_cid, &responder);
  };

  sig_->ServeRequest(kDisconnectionRequest, std::move(on_discon_req));
}

void BrEdrCommandHandler::ServeInformationRequest(
    InformationRequestCallback cb) {
  auto on_info_req = [this, cb = std::move(cb)](
                         const ByteBuffer& request_payload,
                         SignalingChannel::Responder* sig_responder) {
    if (request_payload.size() != sizeof(InformationRequestPayload)) {
      FXL_VLOG(1)
          << "l2cap: BR/EDR cmd: rejecting malformed Information Request, size "
          << request_payload.size();
      sig_responder->RejectNotUnderstood();
      return;
    }

    const auto& info_req = request_payload.As<InformationRequestPayload>();
    const auto type = static_cast<InformationType>(letoh16(info_req.type));
    InformationResponder responder(sig_responder, type);
    cb(type, &responder);
  };

  sig_->ServeRequest(kInformationRequest, std::move(on_info_req));
}

bool BrEdrCommandHandler::ParseReject(
    const ByteBuffer& rej_payload_buf,
    BrEdrCommandHandler::Response* rsp) const {
  auto& rej_payload = rej_payload_buf.As<CommandRejectPayload>();
  rsp->reject_reason_ = static_cast<RejectReason>(letoh16(rej_payload.reason));
  if (rsp->reject_reason() == RejectReason::kInvalidCID) {
    if (rej_payload_buf.size() - sizeof(CommandRejectPayload) < 4) {
      FXL_LOG(ERROR) << "l2cap: BR/EDR cmd: ignoring malformed Command Reject "
                        "Invalid Channel ID, size "
                     << rej_payload_buf.size();
      return false;
    }

    rsp->remote_cid_ = (rej_payload.data[1] << 8) + rej_payload.data[0];
    rsp->local_cid_ = (rej_payload.data[3] << 8) + rej_payload.data[2];
  }
  return true;
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib