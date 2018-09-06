// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_peripheral_server.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/util.h"

#include "helpers.h"

using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;

using fuchsia::bluetooth::le::AdvertisingData;
using fuchsia::bluetooth::le::AdvertisingDataPtr;
using fuchsia::bluetooth::le::Peripheral;
using fuchsia::bluetooth::le::RemoteDevice;
using fuchsia::bluetooth::le::RemoteDevicePtr;

namespace bthost {

namespace {

std::string MessageFromStatus(btlib::hci::Status status) {
  switch (status.error()) {
    case ::btlib::common::HostError::kNoError:
      return "Success";
    case ::btlib::common::HostError::kNotSupported:
      return "Maximum advertisement amount reached";
    case ::btlib::common::HostError::kInvalidParameters:
      return "Advertisement exceeds maximum allowed length";
    default:
      return status.ToString();
  }
}

}  // namespace

LowEnergyPeripheralServer::InstanceData::InstanceData(
    const std::string& id, fxl::WeakPtr<LowEnergyPeripheralServer> owner)
    : id_(id), owner_(owner) {
  ZX_DEBUG_ASSERT(owner_);
}

void LowEnergyPeripheralServer::InstanceData::RetainConnection(
    ConnectionRefPtr conn_ref, RemoteDevice peer) {
  ZX_DEBUG_ASSERT(connectable());
  ZX_DEBUG_ASSERT(!conn_ref_);

  conn_ref_ = std::move(conn_ref);
  owner_->binding()->events().OnCentralConnected(id_, std::move(peer));
}

void LowEnergyPeripheralServer::InstanceData::ReleaseConnection() {
  ZX_DEBUG_ASSERT(connectable());
  ZX_DEBUG_ASSERT(conn_ref_);

  owner_->binding()->events().OnCentralDisconnected(
      conn_ref_->device_identifier());
  conn_ref_ = nullptr;
}

LowEnergyPeripheralServer::LowEnergyPeripheralServer(
    fxl::WeakPtr<::btlib::gap::Adapter> adapter,
    fidl::InterfaceRequest<Peripheral> request)
    : AdapterServerBase(adapter, this, std::move(request)),
      weak_ptr_factory_(this) {}

LowEnergyPeripheralServer::~LowEnergyPeripheralServer() {
  auto* advertising_manager = adapter()->le_advertising_manager();
  ZX_DEBUG_ASSERT(advertising_manager);

  for (const auto& it : instances_) {
    advertising_manager->StopAdvertising(it.first);
  }
}

void LowEnergyPeripheralServer::StartAdvertising(
    AdvertisingData advertising_data, AdvertisingDataPtr scan_result,
    uint32_t interval, bool anonymous, StartAdvertisingCallback callback) {
  auto* advertising_manager = adapter()->le_advertising_manager();
  ZX_DEBUG_ASSERT(advertising_manager);

  ::btlib::gap::AdvertisingData ad_data, scan_data;
  ::btlib::gap::AdvertisingData::FromFidl(advertising_data, &ad_data);
  if (scan_result) {
    ::btlib::gap::AdvertisingData::FromFidl(*scan_result, &scan_data);
  }

  auto self = weak_ptr_factory_.GetWeakPtr();

  ::btlib::gap::LowEnergyAdvertisingManager::ConnectionCallback connect_cb;
  // TODO(armansito): The conversion from hci::Connection to
  // gap::LowEnergyConnectionRef should be performed by a gap library object
  // and not in this layer (see NET-355).
  connect_cb = [self](auto adv_id, auto link) {
    if (self)
      self->OnConnected(std::move(adv_id), std::move(link));
  };
  auto advertising_status_cb = [self, callback = std::move(callback)](
                                   std::string ad_id,
                                   ::btlib::hci::Status status) mutable {
    if (!self)
      return;

    if (!status) {
      bt_log(TRACE, "bt-host", "failed to start advertising: %s",
             status.ToString().c_str());
      callback(fidl_helpers::StatusToFidl(status, MessageFromStatus(status)),
               "");
      return;
    }

    self->instances_[ad_id] =
        InstanceData(ad_id, self->weak_ptr_factory_.GetWeakPtr());
    callback(Status(), ad_id);
  };

  advertising_manager->StartAdvertising(
      ad_data, scan_data, std::move(connect_cb), interval, anonymous,
      std::move(advertising_status_cb));
}

void LowEnergyPeripheralServer::StopAdvertising(
    ::fidl::StringPtr id, StopAdvertisingCallback callback) {
  if (StopAdvertisingInternal(id)) {
    callback(Status());
  } else {
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_FOUND,
                                        "Unrecognized advertisement ID"));
  }
}

bool LowEnergyPeripheralServer::StopAdvertisingInternal(const std::string& id) {
  auto count = instances_.erase(id);
  if (count) {
    adapter()->le_advertising_manager()->StopAdvertising(id);
  }

  return count != 0;
}

void LowEnergyPeripheralServer::OnConnected(std::string advertisement_id,
                                            ::btlib::hci::ConnectionPtr link) {
  ZX_DEBUG_ASSERT(link);

  // If the active adapter that was used to start advertising was changed before
  // we process this connection then the instance will have been removed.
  auto it = instances_.find(advertisement_id);
  if (it == instances_.end()) {
    bt_log(TRACE, "bt-host",
           "connection received from wrong advertising instance");
    return;
  }

  ZX_DEBUG_ASSERT(it->second.connectable());

  auto conn = adapter()->le_connection_manager()->RegisterRemoteInitiatedLink(
      std::move(link));
  if (!conn) {
    bt_log(TRACE, "bt-host", "incoming connection rejected");
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  conn->set_closed_callback([self, id = advertisement_id] {
    bt_log(TRACE, "bt-host", "central disconnected");

    if (!self)
      return;

    // Make sure that the instance hasn't been removed.
    auto it = self->instances_.find(id);
    if (it == self->instances_.end())
      return;

    // This sends OnCentralDisconnected() to the delegate.
    it->second.ReleaseConnection();
  });

  // A RemoteDevice will have been created for the new connection.
  auto* device =
      adapter()->device_cache().FindDeviceById(conn->device_identifier());
  ZX_DEBUG_ASSERT(device);

  bt_log(TRACE, "bt-host", "central connected");
  RemoteDevicePtr remote_device =
      fidl_helpers::NewLERemoteDevice(std::move(*device));
  ZX_DEBUG_ASSERT(remote_device);
  it->second.RetainConnection(std::move(conn), std::move(*remote_device));
}

}  // namespace bthost
