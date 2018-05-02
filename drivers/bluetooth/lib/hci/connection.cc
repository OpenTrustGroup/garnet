// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace hci {
namespace {

std::string LinkTypeToString(Connection::LinkType type) {
  switch (type) {
    case Connection::LinkType::kACL:
      return "ACL";
    case Connection::LinkType::kSCO:
      return "SCO";
    case Connection::LinkType::kESCO:
      return "ESCO";
    case Connection::LinkType::kLE:
      return "LE";
  }

  FXL_NOTREACHED();
  return "(invalid)";
}

}  // namespace

Connection::Connection(ConnectionHandle handle,
                       Role role,
                       const common::DeviceAddress& peer_address,
                       const LEConnectionParameters& params,
                       fxl::RefPtr<Transport> hci)
    : ll_type_(LinkType::kLE),
      handle_(handle),
      role_(role),
      is_open_(true),
      peer_address_(peer_address),
      le_params_(params),
      hci_(hci),
      weak_ptr_factory_(this) {
  FXL_DCHECK(handle_);
  FXL_DCHECK(hci_);
}

Connection::~Connection() {
  Close();
}

void Connection::Close(StatusCode reason) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (!is_open())
    return;

  // The connection is immediately marked as closed as there is no reasonable
  // way for a Disconnect procedure to fail, i.e. it always succeeds. If the
  // controller reports failure in the Disconnection Complete event, it should
  // be because we gave it an already disconnected handle which we would treat
  // as success.
  //
  // TODO(armansito): The procedure could also fail if "the command was not
  // presently allowed". Retry in that case?
  set_closed();

  // Here we send a HCI_Disconnect command without waiting for it to complete.

  auto status_cb = [](auto id, const EventPacket& event) {
    FXL_DCHECK(event.event_code() == kCommandStatusEventCode);
    const auto& params = event.view().payload<CommandStatusEventParams>();
    if (params.status != StatusCode::kSuccess) {
      FXL_LOG(WARNING) << fxl::StringPrintf(
          "Ignoring failed disconnection status: 0x%02x", params.status);
    }
  };

  auto disconn =
      CommandPacket::New(kDisconnect, sizeof(DisconnectCommandParams));
  auto params =
      disconn->mutable_view()->mutable_payload<DisconnectCommandParams>();
  params->connection_handle = htole16(handle_);
  params->reason = reason;

  hci_->command_channel()->SendCommand(
      std::move(disconn), async_get_default(),
      status_cb, kCommandStatusEventCode);
}

std::string Connection::ToString() const {
  return fxl::StringPrintf(
      "(%s link - handle: 0x%04x, role: %s, address: %s, "
      "interval: %.2f ms, latency: %.2f ms, timeout: %u ms)",
      LinkTypeToString(ll_type_).c_str(), handle_,
      role_ == Role::kMaster ? "master" : "slave",
      peer_address_.ToString().c_str(),
      static_cast<float>(le_params_.interval()) * 1.25f,
      static_cast<float>(le_params_.latency()) * 1.25f,
      le_params_.supervision_timeout() * 10u);
}

}  // namespace hci
}  // namespace btlib
