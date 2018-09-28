// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host_server.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/host/gatt_host.h"
#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/sm/util.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

#include "helpers.h"
#include "low_energy_central_server.h"
#include "low_energy_peripheral_server.h"
#include "profile_server.h"

namespace bthost {

using btlib::sm::IOCapability;
using fuchsia::bluetooth::Bool;
using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;
using fuchsia::bluetooth::control::AdapterState;
using fuchsia::bluetooth::control::BondingData;
using fuchsia::bluetooth::control::Key;
using fuchsia::bluetooth::control::LEData;
using fuchsia::bluetooth::control::LTK;
using fuchsia::bluetooth::control::RemoteDevice;

HostServer::HostServer(zx::channel channel,
                       fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                       fbl::RefPtr<GattHost> gatt_host)
    : AdapterServerBase(adapter, this, std::move(channel)),
      pairing_delegate_(nullptr),
      gatt_host_(gatt_host),
      requesting_discovery_(false),
      requesting_discoverable_(false),
      io_capability_(IOCapability::kNoInputNoOutput),
      weak_ptr_factory_(this) {
  ZX_DEBUG_ASSERT(gatt_host_);

  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter->remote_device_cache()->set_device_updated_callback(
      [self](const auto& device) {
        if (self) {
          self->OnRemoteDeviceUpdated(device);
        }
      });
  adapter->remote_device_cache()->set_device_removed_callback(
      [self = weak_ptr_factory_.GetWeakPtr()](const auto& identifier) {
        if (self) {
          self->OnRemoteDeviceRemoved(identifier);
        }
      });
  adapter->remote_device_cache()->set_device_bonded_callback(
      [self = weak_ptr_factory_.GetWeakPtr()](const auto& device) {
        if (self) {
          self->OnRemoteDeviceBonded(device);
        }
      });
}

HostServer::~HostServer() { Close(); }

void HostServer::GetInfo(GetInfoCallback callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter()));
}

void HostServer::ListDevices(ListDevicesCallback callback) {
  std::vector<RemoteDevice> fidl_devices;
  adapter()->remote_device_cache()->ForEach(
      [&fidl_devices](const btlib::gap::RemoteDevice& dev) {
        if (dev.connectable()) {
          fidl_devices.push_back(fidl_helpers::NewRemoteDevice(dev));
        }
      });
  callback(fidl::VectorPtr<RemoteDevice>(std::move(fidl_devices)));
}

void HostServer::SetLocalName(::fidl::StringPtr local_name,
                              SetLocalNameCallback callback) {
  ZX_DEBUG_ASSERT(!local_name.is_null());
  // Make a copy of |local_name| to move separately into the lambda.
  std::string name_copy(*local_name);
  adapter()->SetLocalName(
      std::move(local_name),
      [self = weak_ptr_factory_.GetWeakPtr(), local_name = std::move(name_copy),
       callback = std::move(callback)](auto status) {
        // Send adapter state update on success and if the connection is still
        // open.
        if (status && self) {
          AdapterState state;
          state.local_name = std::move(local_name);
          self->binding()->events().OnAdapterStateChanged(std::move(state));
        }
        callback(fidl_helpers::StatusToFidl(status, "Can't Set Local Name"));
      });
}

void HostServer::StartLEDiscovery(StartDiscoveryCallback callback) {
  auto le_manager = adapter()->le_discovery_manager();
  if (!le_manager) {
    callback(fidl_helpers::NewFidlError(ErrorCode::BAD_STATE,
                                        "Adapter is not initialized yet."));
    return;
  }
  le_manager->StartDiscovery([self = weak_ptr_factory_.GetWeakPtr(),
                              callback = std::move(callback)](auto session) {
    // End the new session if this AdapterServer got destroyed in the
    // mean time (e.g. because the client disconnected).
    if (!self) {
      callback(
          fidl_helpers::NewFidlError(ErrorCode::FAILED, "Adapter Shutdown"));
      return;
    }

    if (!self->requesting_discovery_) {
      callback(
          fidl_helpers::NewFidlError(ErrorCode::CANCELED, "Request canceled"));
      return;
    }

    if (!session) {
      bt_log(TRACE, "bt-host", "failed to start LE discovery session");
      callback(fidl_helpers::NewFidlError(
          ErrorCode::FAILED, "Failed to start LE discovery session"));
      self->bredr_discovery_session_ = nullptr;
      self->requesting_discovery_ = false;
      return;
    }

    // Set up a general-discovery filter for connectable devices.
    // NOTE(armansito): This currently has no effect since OnDeviceUpdated
    // events are generated based on RemoteDeviceCache events. |session|'s
    // "result callback" is unused.
    session->filter()->set_connectable(true);
    session->filter()->SetGeneralDiscoveryFlags();

    self->le_discovery_session_ = std::move(session);
    self->requesting_discovery_ = false;

    // Send the adapter state update.
    AdapterState state;
    state.discovering = Bool::New();
    state.discovering->value = true;
    self->binding()->events().OnAdapterStateChanged(std::move(state));

    callback(Status());
  });
}

void HostServer::StartDiscovery(StartDiscoveryCallback callback) {
  bt_log(TRACE, "bt-host", "StartDiscovery()");
  ZX_DEBUG_ASSERT(adapter());

  if (le_discovery_session_ || requesting_discovery_) {
    bt_log(TRACE, "bt-host", "discovery already in progress");
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "Discovery already in progress"));
    return;
  }

  requesting_discovery_ = true;
  auto bredr_manager = adapter()->bredr_discovery_manager();
  if (!bredr_manager) {
    StartLEDiscovery(std::move(callback));
    return;
  }
  // TODO(jamuraa): start these in parallel instead of sequence
  bredr_manager->RequestDiscovery(
      [self = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)](
          btlib::hci::Status status, auto session) mutable {
        if (!self) {
          callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Adapter Shutdown"));
          return;
        }

        if (!self->requesting_discovery_) {
          callback(fidl_helpers::NewFidlError(ErrorCode::CANCELED,
                                              "Request Canceled"));
          return;
        }

        if (!status || !session) {
          bt_log(TRACE, "bt-host", "failed to start BR/EDR discovery session");
          callback(fidl_helpers::StatusToFidl(
              status, "Failed to start BR/EDR discovery session"));
          self->requesting_discovery_ = false;
          return;
        }

        self->bredr_discovery_session_ = std::move(session);
        self->StartLEDiscovery(std::move(callback));
      });
}

void HostServer::StopDiscovery(StopDiscoveryCallback callback) {
  bt_log(TRACE, "bt-host", "StopDiscovery()");
  if (!le_discovery_session_) {
    bt_log(TRACE, "bt-host", "no active discovery session");
    callback(fidl_helpers::NewFidlError(ErrorCode::BAD_STATE,
                                        "No discovery session in progress"));
    return;
  }

  bredr_discovery_session_ = nullptr;
  le_discovery_session_ = nullptr;

  AdapterState state;
  state.discovering = Bool::New();
  state.discovering->value = false;
  this->binding()->events().OnAdapterStateChanged(std::move(state));

  callback(Status());
}

void HostServer::SetConnectable(bool connectable,
                                SetConnectableCallback callback) {
  bt_log(TRACE, "bt-host", "SetConnectable(%s)",
         connectable ? "true" : "false");

  auto bredr_conn_manager = adapter()->bredr_connection_manager();
  if (!bredr_conn_manager) {
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_SUPPORTED,
                                        "Connectable mode not available"));
    return;
  }
  bredr_conn_manager->SetConnectable(
      connectable, [callback = std::move(callback)](const auto& status) {
        callback(fidl_helpers::StatusToFidl(status));
      });
}

void HostServer::AddBondedDevices(
    ::fidl::VectorPtr<fuchsia::bluetooth::control::BondingData> bonds,
    AddBondedDevicesCallback callback) {
  bt_log(TRACE, "bt-host", "AddBondedDevices");
  if (!bonds) {
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_SUPPORTED,
                                        "No bonds were added"));
    return;
  }

  for (auto& bond : *bonds) {
    btlib::sm::PairingData bond_data;
    if (bond.le) {
      bond_data = fidl_helpers::PairingDataFromFidl(*bond.le);

      // Report error if bond data is missing a security key.
      if (!bond_data.irk && !bond_data.csrk) {
        bt_log(ERROR, "bt-host", "LE bond data is missing security key");
        callback(fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                            "LE IRK/CSRK missing"));
        return;
      }
    }

    // TODO(armansito): Handle BR/EDR data here. For now we skip the entry if LE
    // data isn't available.
    if (!bond_data.identity_address) {
      bt_log(ERROR, "bt-host", "LE bonding data is required");
      continue;
    }

    // TODO(armansito): BondingData should contain the identity address for both
    // transports instead of storing them separately. For now use the one we
    // obtained from |bond.le|.
    if (!adapter()->AddBondedDevice(bond.identifier,
                                    *bond_data.identity_address, bond_data)) {
      // TODO(armansito): Continue walking the list if this fails?
      callback(fidl_helpers::NewFidlError(
          ErrorCode::FAILED,
          fxl::StringPrintf("Failed to initialize bonded device (id: %s)",
                            bond.identifier->c_str())));
      return;
    }
  }

  callback(Status());
}

void HostServer::OnRemoteDeviceBonded(
    const ::btlib::gap::RemoteDevice& remote_device) {
  bt_log(TRACE, "bt-host", "OnRemoteDeviceBonded()");
  binding()->events().OnNewBondingData(
      fidl_helpers::NewBondingData(remote_device));
}

void HostServer::SetDiscoverable(bool discoverable,
                                 SetDiscoverableCallback callback) {
  bt_log(TRACE, "bt-host", "SetDiscoverable(%s)",
         discoverable ? "true" : "false");
  // TODO(NET-830): advertise LE here
  if (!discoverable) {
    bredr_discoverable_session_ = nullptr;

    AdapterState state;
    state.discoverable = Bool::New();
    state.discoverable->value = false;
    this->binding()->events().OnAdapterStateChanged(std::move(state));

    callback(Status());
    return;
  }
  if (discoverable && requesting_discoverable_) {
    bt_log(TRACE, "bt-host", "SetDiscoverable already in progress");
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "SetDiscoverable already in progress"));
    return;
  }
  requesting_discoverable_ = true;
  auto bredr_manager = adapter()->bredr_discovery_manager();
  if (!bredr_manager) {
    callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                        "Discoverable mode not available"));
    return;
  }
  bredr_manager->RequestDiscoverable(
      [self = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)](
          btlib::hci::Status status, auto session) {
        if (!self) {
          callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Adapter Shutdown"));
          return;
        }

        if (!self->requesting_discoverable_) {
          callback(fidl_helpers::NewFidlError(ErrorCode::CANCELED,
                                              "Request canceled"));
          return;
        }

        if (!status || !session) {
          bt_log(TRACE, "bt-host", "failed to set discoverable");
          callback(
              fidl_helpers::StatusToFidl(status, "Failed to set discoverable"));
          self->requesting_discoverable_ = false;
          return;
        }

        self->bredr_discoverable_session_ = std::move(session);
        AdapterState state;
        state.discoverable = Bool::New();
        state.discoverable->value = true;
        self->binding()->events().OnAdapterStateChanged(std::move(state));
        callback(Status());
      });
}

void HostServer::RequestLowEnergyCentral(
    fidl::InterfaceRequest<fuchsia::bluetooth::le::Central> request) {
  BindServer<LowEnergyCentralServer>(std::move(request), gatt_host_);
}

void HostServer::RequestLowEnergyPeripheral(
    fidl::InterfaceRequest<fuchsia::bluetooth::le::Peripheral> request) {
  BindServer<LowEnergyPeripheralServer>(std::move(request));
}

void HostServer::RequestGattServer(
    fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> request) {
  // GATT FIDL requests are handled by GattHost.
  gatt_host_->BindGattServer(std::move(request));
}

void HostServer::SetPairingDelegate(
    ::fuchsia::bluetooth::control::InputCapabilityType input,
    ::fuchsia::bluetooth::control::OutputCapabilityType output,
    ::fidl::InterfaceHandle<::fuchsia::bluetooth::control::PairingDelegate>
        delegate) {
  bool cleared = !delegate;
  pairing_delegate_.Bind(std::move(delegate));

  if (cleared) {
    bt_log(TRACE, "bt-host", "PairingDelegate cleared");
    ResetPairingDelegate();
    return;
  }

  io_capability_ = fidl_helpers::IoCapabilityFromFidl(input, output);
  bt_log(TRACE, "bt-host", "PairingDelegate assigned (I/O capability: %s)",
         btlib::sm::util::IOCapabilityToString(io_capability_).c_str());

  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter()->SetPairingDelegate(self);
  pairing_delegate_.set_error_handler([self] {
    bt_log(TRACE, "bt-host", "PairingDelegate disconnected");
    if (self) {
      self->ResetPairingDelegate();
    }
  });
}

void HostServer::RequestProfile(
    fidl::InterfaceRequest<fuchsia::bluetooth::bredr::Profile> request) {
  BindServer<ProfileServer>(std::move(request));
}

void HostServer::Close() {
  bt_log(TRACE, "bt-host", "closing FIDL handles");

  // Destroy all FIDL bindings.
  servers_.clear();
  gatt_host_->CloseServers();

  // Cancel pending requests.
  requesting_discovery_ = false;
  requesting_discoverable_ = false;

  // Diff for final adapter state update.
  bool send_update = false;
  AdapterState state;

  // Stop all procedures initiated via host.
  if (le_discovery_session_ || bredr_discovery_session_) {
    send_update = true;
    le_discovery_session_ = nullptr;
    bredr_discovery_session_ = nullptr;

    state.discovering = Bool::New();
    state.discovering->value = false;
  }

  if (bredr_discoverable_session_) {
    send_update = true;
    bredr_discoverable_session_ = nullptr;

    state.discoverable = Bool::New();
    state.discoverable->value = false;
  }

  // TODO(NET-1092): Clean up connections here as well.
  // TODO(armansito): Clear auto-connectable devices.

  // Disallow future pairing.
  pairing_delegate_ = nullptr;
  ResetPairingDelegate();

  // Send adapter state change.
  if (send_update) {
    binding()->events().OnAdapterStateChanged(std::move(state));
  }
}

btlib::sm::IOCapability HostServer::io_capability() const {
  bt_log(TRACE, "bt-host", "I/O capability: %s",
         btlib::sm::util::IOCapabilityToString(io_capability_).c_str());
  return io_capability_;
}

void HostServer::CompletePairing(std::string id, btlib::sm::Status status) {
  bt_log(INFO, "bt-host", "pairing complete for device: %s, status: %s",
         id.c_str(), status.ToString().c_str());
  ZX_DEBUG_ASSERT(pairing_delegate_);
  pairing_delegate_->OnPairingComplete(std::move(id), fidl_helpers::StatusToFidl(status));
}

void HostServer::ConfirmPairing(std::string id, ConfirmCallback confirm) {
  bt_log(INFO, "bt-host", "pairing request for device: %s", id.c_str());
  auto found_device = adapter()->remote_device_cache()->FindDeviceById(id);
  ZX_DEBUG_ASSERT(found_device);
  auto device = fidl_helpers::NewRemoteDevicePtr(*found_device);
  ZX_DEBUG_ASSERT(device);

  ZX_DEBUG_ASSERT(pairing_delegate_);
  pairing_delegate_->OnPairingRequest(
      std::move(*device), fuchsia::bluetooth::control::PairingMethod::CONSENT,
      nullptr,
      [confirm = std::move(confirm)](
          const bool success, const std::string passkey) { confirm(success); });
}

void HostServer::DisplayPasskey(std::string id, uint32_t passkey,
                                ConfirmCallback confirm) {
  bt_log(INFO, "bt-host", "pairing request for device: %s", id.c_str());
  bt_log(INFO, "bt-host", "enter passkey: %06u", passkey);

  auto device = fidl_helpers::NewRemoteDevicePtr(
      *adapter()->remote_device_cache()->FindDeviceById(id));
  ZX_DEBUG_ASSERT(device);

  ZX_DEBUG_ASSERT(pairing_delegate_);
  pairing_delegate_->OnPairingRequest(
      std::move(*device),
      fuchsia::bluetooth::control::PairingMethod::PASSKEY_DISPLAY,
      fxl::StringPrintf("%06u", passkey),
      [confirm = std::move(confirm)](
          const bool success, const std::string passkey) { confirm(success); });
}

void HostServer::RequestPasskey(std::string id,
                                PasskeyResponseCallback respond) {
  auto device = fidl_helpers::NewRemoteDevicePtr(
      *adapter()->remote_device_cache()->FindDeviceById(id));
  ZX_DEBUG_ASSERT(device);

  ZX_DEBUG_ASSERT(pairing_delegate_);
  pairing_delegate_->OnPairingRequest(
      std::move(*device),
      fuchsia::bluetooth::control::PairingMethod::PASSKEY_ENTRY, nullptr,
      [respond = std::move(respond)](const bool success,
                                     const std::string passkey) {
        if (!success) {
          respond(-1);
        } else {
          uint32_t response;
          if (!fxl::StringToNumberWithError<uint32_t>(passkey, &response)) {
            bt_log(ERROR, "bt-host", "Unrecognized integer in string: %s", passkey.c_str());
            respond(-1);
          } else {
            respond(response);
          }
        }
      });
}

void HostServer::OnConnectionError(Server* server) {
  ZX_DEBUG_ASSERT(server);
  servers_.erase(server);
}

void HostServer::OnRemoteDeviceUpdated(
    const ::btlib::gap::RemoteDevice& remote_device) {
  if (!remote_device.connectable()) {
    return;
  }

  auto fidl_device = fidl_helpers::NewRemoteDevicePtr(remote_device);
  if (!fidl_device) {
    bt_log(TRACE, "bt-host", "ignoring malformed device update");
    return;
  }

  this->binding()->events().OnDeviceUpdated(std::move(*fidl_device));
}

void HostServer::OnRemoteDeviceRemoved(const std::string& identifier) {
  // TODO(armansito): Notify only if the device is connectable for symmetry with
  // OnDeviceUpdated?
  this->binding()->events().OnDeviceRemoved(identifier);
}

void HostServer::ResetPairingDelegate() {
  io_capability_ = IOCapability::kNoInputNoOutput;
  adapter()->SetPairingDelegate(fxl::WeakPtr<HostServer>());
}

}  // namespace bthost
