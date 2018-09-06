// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "lib/fxl/strings/string_printf.h"

#include "command_channel.h"
#include "defaults.h"
#include "transport.h"

namespace btlib {
namespace hci {

// Production implementation of the Connection class against a HCI transport.
class ConnectionImpl final : public Connection {
 public:
  ConnectionImpl(ConnectionHandle handle, LinkType ll_type, Role role,
                 const common::DeviceAddress& local_address,
                 const common::DeviceAddress& peer_address,
                 fxl::RefPtr<Transport> hci);
  ~ConnectionImpl() override;

  // Connection overrides:
  fxl::WeakPtr<Connection> WeakPtr() override;
  void Close(StatusCode reason) override;
  bool StartEncryption() override;

 private:
  // Starts the LE link layer authentication procedure using the given |ltk|.
  bool LEStartEncryption(const LinkKey& ltk);

  // Called when encryption is enabled or disabled as a result of the link layer
  // encryption "start" or "pause" procedure. If |status| indicates failure,
  // then this method will disconnect the link before notifying the encryption
  // change handler.
  void HandleEncryptionStatus(Status status, bool enabled);

  // HCI event handlers.
  void OnEncryptionChangeEvent(const EventPacket& event);
  void OnEncryptionKeyRefreshCompleteEvent(const EventPacket& event);
  void OnLELongTermKeyRequestEvent(const EventPacket& event);

  fxl::ThreadChecker thread_checker_;

  // IDs for encryption related HCI event handlers.
  CommandChannel::EventHandlerId enc_change_id_;
  CommandChannel::EventHandlerId enc_key_refresh_cmpl_id_;
  CommandChannel::EventHandlerId le_ltk_request_id_;

  // The underlying HCI transport.
  fxl::RefPtr<Transport> hci_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<ConnectionImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConnectionImpl);
};

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

  ZX_PANIC("invalid link type: %u", static_cast<unsigned int>(type));
  return "(invalid)";
}

template <void (ConnectionImpl::*EventHandlerMethod)(const EventPacket&)>
CommandChannel::EventCallback BindEventHandler(
    fxl::WeakPtr<ConnectionImpl> conn) {
  return [conn](const auto& event) {
    if (conn) {
      ((conn.get())->*EventHandlerMethod)(event);
    }
  };
}

}  // namespace

// ====== Connection member methods  =====

// static
std::unique_ptr<Connection> Connection::CreateLE(
    ConnectionHandle handle, Role role,
    const common::DeviceAddress& local_address,
    const common::DeviceAddress& peer_address,
    const LEConnectionParameters& params, fxl::RefPtr<Transport> hci) {
  ZX_DEBUG_ASSERT(local_address.type() != common::DeviceAddress::Type::kBREDR);
  ZX_DEBUG_ASSERT(peer_address.type() != common::DeviceAddress::Type::kBREDR);
  auto conn = std::make_unique<ConnectionImpl>(
      handle, LinkType::kLE, role, local_address, peer_address, hci);
  conn->set_low_energy_parameters(params);
  return conn;
}

// static
std::unique_ptr<Connection> Connection::CreateACL(
    ConnectionHandle handle, Role role,
    const common::DeviceAddress& local_address,
    const common::DeviceAddress& peer_address, fxl::RefPtr<Transport> hci) {
  ZX_DEBUG_ASSERT(local_address.type() == common::DeviceAddress::Type::kBREDR);
  ZX_DEBUG_ASSERT(peer_address.type() == common::DeviceAddress::Type::kBREDR);
  auto conn = std::make_unique<ConnectionImpl>(
      handle, LinkType::kACL, role, local_address, peer_address, hci);
  return conn;
}

Connection::Connection(ConnectionHandle handle, LinkType ll_type, Role role,
                       const common::DeviceAddress& local_address,
                       const common::DeviceAddress& peer_address)
    : ll_type_(ll_type),
      handle_(handle),
      role_(role),
      is_open_(true),
      local_address_(local_address),
      peer_address_(peer_address) {
  ZX_DEBUG_ASSERT(handle_);
}

std::string Connection::ToString() const {
  std::string params = "";
  if (ll_type() == LinkType::kLE) {
    params = ", " + le_params_.ToString();
  }
  return fxl::StringPrintf("(%s link - handle: %#.4x, role: %s, address: %s%s)",
                           LinkTypeToString(ll_type_).c_str(), handle_,
                           role_ == Role::kMaster ? "master" : "slave",
                           peer_address_.ToString().c_str(), params.c_str());
}

// ====== ConnectionImpl member methods ======

ConnectionImpl::ConnectionImpl(ConnectionHandle handle, LinkType ll_type,
                               Role role,
                               const common::DeviceAddress& local_address,
                               const common::DeviceAddress& peer_address,
                               fxl::RefPtr<Transport> hci)
    : Connection(handle, ll_type, role, local_address, peer_address),
      hci_(hci),
      weak_ptr_factory_(this) {
  ZX_DEBUG_ASSERT(hci_);

  auto self = weak_ptr_factory_.GetWeakPtr();

  enc_change_id_ = hci_->command_channel()->AddEventHandler(
      kEncryptionChangeEventCode,
      BindEventHandler<&ConnectionImpl::OnEncryptionChangeEvent>(self),
      async_get_default_dispatcher());

  enc_key_refresh_cmpl_id_ = hci_->command_channel()->AddEventHandler(
      kEncryptionKeyRefreshCompleteEventCode,
      BindEventHandler<&ConnectionImpl::OnEncryptionKeyRefreshCompleteEvent>(
          self),
      async_get_default_dispatcher());

  le_ltk_request_id_ = hci_->command_channel()->AddLEMetaEventHandler(
      kLELongTermKeyRequestSubeventCode,
      BindEventHandler<&ConnectionImpl::OnLELongTermKeyRequestEvent>(self),
      async_get_default_dispatcher());
}

ConnectionImpl::~ConnectionImpl() {
  // Tell ACL data channel to clear all ACL buffering state related to this
  // link.
  hci_->acl_data_channel()->ClearLinkState(handle());

  // Unregister HCI event handlers.
  hci_->command_channel()->RemoveEventHandler(enc_change_id_);
  hci_->command_channel()->RemoveEventHandler(enc_key_refresh_cmpl_id_);
  hci_->command_channel()->RemoveEventHandler(le_ltk_request_id_);

  Close(StatusCode::kRemoteUserTerminatedConnection);
}

fxl::WeakPtr<Connection> ConnectionImpl::WeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void ConnectionImpl::Close(StatusCode reason) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
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
    ZX_DEBUG_ASSERT(event.event_code() == kCommandStatusEventCode);
    const auto& params = event.view().payload<CommandStatusEventParams>();
    if (params.status != StatusCode::kSuccess) {
      bt_log(WARN, "hci", "ignoring failed disconnection status: %#.2x",
             params.status);
    }
  };

  auto disconn =
      CommandPacket::New(kDisconnect, sizeof(DisconnectCommandParams));
  auto params =
      disconn->mutable_view()->mutable_payload<DisconnectCommandParams>();
  params->connection_handle = htole16(handle());
  params->reason = reason;

  hci_->command_channel()->SendCommand(std::move(disconn), async_get_default_dispatcher(),
                                       std::move(status_cb),
                                       kCommandStatusEventCode);
}

bool ConnectionImpl::StartEncryption() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  if (!is_open()) {
    bt_log(TRACE, "hci", "connection closed; cannot start encryption");
    return false;
  }

  if (ll_type() != LinkType::kLE) {
    bt_log(TRACE, "hci", "encrypting BR/EDR links not supported");

    // TODO(armansito): Support this.
    return false;
  }

  if (role() != Role::kMaster) {
    bt_log(TRACE, "hci", "only the master can start encryption");
    return false;
  }

  if (!ltk()) {
    bt_log(TRACE, "hci", "connection has no LTK; cannot start encryption");
    return false;
  }

  return LEStartEncryption(*ltk());
}

bool ConnectionImpl::LEStartEncryption(const LinkKey& ltk) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());

  // TODO(NET-1042): Tell the data channel to stop data flow.

  auto cmd = CommandPacket::New(kLEStartEncryption,
                                sizeof(LEStartEncryptionCommandParams));
  auto* params =
      cmd->mutable_view()->mutable_payload<LEStartEncryptionCommandParams>();
  params->connection_handle = htole16(handle());
  params->random_number = htole64(ltk.rand());
  params->encrypted_diversifier = htole16(ltk.ediv());
  params->long_term_key = ltk.value();

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto status_cb = [self](auto id, const EventPacket& event) {
    if (!self) {
      return;
    }

    Status status = event.ToStatus();
    if (status) {
      bt_log(TRACE, "hci-le", "began authentication procedure");
      return;
    }

    bt_log(ERROR, "hci", "failed to start LE authentication: %s",
           status.ToString().c_str());
    if (self->encryption_change_callback()) {
      self->encryption_change_callback()(status, false);
    }
  };

  return hci_->command_channel()->SendCommand(
             std::move(cmd), async_get_default_dispatcher(), std::move(status_cb),
             kCommandStatusEventCode) != 0u;
}

void ConnectionImpl::HandleEncryptionStatus(Status status, bool enabled) {
  // "On an authentication failure, the connection shall be automatically
  // disconnected by the Link Layer." (HCI_LE_Start_Encryption, Vol 2, Part E,
  // 7.8.24). We make sure of this by telling the controller to disconnect.
  // TODO(armansito): Do the same thing for ACL links?
  if (!status && ll_type() == LinkType::kLE) {
    Close(StatusCode::kAuthenticationFailure);
  } else {
    // TODO(NET-1042): Tell the data channel to resume data flow.
  }

  encryption_change_callback()(status, enabled);
}

void ConnectionImpl::OnEncryptionChangeEvent(const EventPacket& event) {
  ZX_DEBUG_ASSERT(event.event_code() == kEncryptionChangeEventCode);
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());

  if (!encryption_change_callback()) {
    bt_log(TRACE, "hci", "encryption changed event ignored");
    return;
  }

  if (event.view().payload_size() != sizeof(EncryptionChangeEventParams)) {
    bt_log(WARN, "hci", "malformed encryption change event");
    return;
  }

  const auto& params = event.view().payload<EncryptionChangeEventParams>();
  hci::ConnectionHandle handle = le16toh(params.connection_handle);

  // Silently ignore the event as it isn't meant for this connection.
  if (handle != this->handle()) {
    return;
  }

  if (!is_open()) {
    bt_log(TRACE, "hci", "encryption change ignored: connection closed");
    return;
  }

  Status status(params.status);
  bool enabled = params.encryption_enabled != 0;

  bt_log(TRACE, "hci", "encryption change (%s) %s",
         enabled ? "enabled" : "disabled", status.ToString().c_str());

  // TODO(NET-1042): Tell the data channel to resume data flow.

  HandleEncryptionStatus(status, enabled);
}

void ConnectionImpl::OnEncryptionKeyRefreshCompleteEvent(
    const EventPacket& event) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(event.event_code() == kEncryptionKeyRefreshCompleteEventCode);

  if (!encryption_change_callback()) {
    bt_log(TRACE, "hci", "encryption key refresh event ignored");
    return;
  }

  if (event.view().payload_size() !=
      sizeof(EncryptionKeyRefreshCompleteEventParams)) {
    bt_log(WARN, "hci", "malformed encryption key refresh complete event");
    return;
  }

  const auto& params =
      event.view().payload<EncryptionKeyRefreshCompleteEventParams>();
  hci::ConnectionHandle handle = le16toh(params.connection_handle);

  // Silently ignore this event as it isn't meant for this connection.
  if (handle != this->handle()) {
    return;
  }

  if (!is_open()) {
    bt_log(TRACE, "hci", "encryption key refresh ignored: connection closed");
    return;
  }

  Status status(params.status);

  bt_log(TRACE, "hci", "encryption key refresh %s", status.ToString().c_str());

  // Report that encryption got disabled on failure status. The accuracy of this
  // isn't that important since the link will be disconnected.
  HandleEncryptionStatus(status, static_cast<bool>(status));
}

void ConnectionImpl::OnLELongTermKeyRequestEvent(const EventPacket& event) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(event.event_code() == kLEMetaEventCode);
  ZX_DEBUG_ASSERT(event.view().payload<LEMetaEventParams>().subevent_code ==
                  kLELongTermKeyRequestSubeventCode);

  auto* params = event.le_event_params<LELongTermKeyRequestSubeventParams>();
  if (!params) {
    bt_log(WARN, "hci", "malformed LE LTK request event");
    return;
  }

  hci::ConnectionHandle handle = le16toh(params->connection_handle);

  // Silently ignore the event as it isn't meant for this connection.
  if (handle != this->handle()) {
    return;
  }

  // TODO(NET-1042): Tell the data channel to stop data flow.

  std::unique_ptr<CommandPacket> cmd;

  uint16_t rand = le16toh(params->random_number);
  uint64_t ediv = le64toh(params->encrypted_diversifier);
  if (ltk() && ltk()->rand() == rand && ltk()->ediv() == ediv) {
    cmd = CommandPacket::New(kLELongTermKeyRequestReply,
                             sizeof(LELongTermKeyRequestReplyCommandParams));
    auto* params =
        cmd->mutable_view()
            ->mutable_payload<LELongTermKeyRequestReplyCommandParams>();

    params->connection_handle = htole16(handle);
    params->long_term_key = ltk()->value();
  } else {
    bt_log(TRACE, "hci-le", "LTK request rejected");

    cmd = CommandPacket::New(
        kLELongTermKeyRequestNegativeReply,
        sizeof(LELongTermKeyRequestNegativeReplyCommandParams));
    auto* params =
        cmd->mutable_view()
            ->mutable_payload<LELongTermKeyRequestNegativeReplyCommandParams>();
    params->connection_handle = htole16(handle);
  }

  auto status_cb = [](auto id, const EventPacket& event) {
    hci_is_error(event, TRACE, "hci-le", "failed to reply to LTK request");
  };
  hci_->command_channel()->SendCommand(
      std::move(cmd), async_get_default_dispatcher(), std::move(status_cb));
}

}  // namespace hci
}  // namespace btlib
