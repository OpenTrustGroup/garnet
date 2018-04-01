// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include <fuchsia/cpp/bluetooth_low_energy.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertising_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"

namespace bthost {

// Implements the low_energy::Central FIDL interface.
class LowEnergyPeripheralServer
    : public AdapterServerBase<bluetooth_low_energy::Peripheral> {
 public:
  LowEnergyPeripheralServer(
      fxl::WeakPtr<::btlib::gap::Adapter> adapter,
      fidl::InterfaceRequest<bluetooth_low_energy::Peripheral> request);
  ~LowEnergyPeripheralServer() override;

 private:
  using DelegatePtr = bluetooth_low_energy::PeripheralDelegatePtr;
  using ConnectionRefPtr = ::btlib::gap::LowEnergyConnectionRefPtr;

  class InstanceData final {
   public:
    InstanceData() = default;
    InstanceData(const std::string& id, DelegatePtr delegate);

    InstanceData(InstanceData&& other) = default;
    InstanceData& operator=(InstanceData&& other) = default;

    bool connectable() const { return static_cast<bool>(delegate_); }

    // Takes ownership of |conn_ref| and notifies the delegate of the new
    // connection.
    void RetainConnection(ConnectionRefPtr conn_ref,
                          bluetooth_low_energy::RemoteDevice central);

    // Deletes the connetion reference and notifies the delegate of
    // disconnection.
    void ReleaseConnection();

   private:
    std::string id_;
    DelegatePtr delegate_;
    ConnectionRefPtr conn_ref_;

    FXL_DISALLOW_COPY_AND_ASSIGN(InstanceData);
  };

  // bluetooth_low_energy::Peripheral overrides:
  void StartAdvertising(
      bluetooth_low_energy::AdvertisingData advertising_data,
      bluetooth_low_energy::AdvertisingDataPtr scan_result,
      ::fidl::InterfaceHandle<bluetooth_low_energy::PeripheralDelegate>
          delegate,
      uint32_t interval,
      bool anonymous,
      StartAdvertisingCallback callback) override;

  void StopAdvertising(::fidl::StringPtr advertisement_id,
                       StopAdvertisingCallback callback) override;
  bool StopAdvertisingInternal(const std::string& id);

  // Called when a central connects to us.  When this is called, the
  // advertisement in |advertisement_id| has been stopped.
  void OnConnected(std::string advertisement_id,
                   ::btlib::hci::ConnectionPtr link);

  // Tracks currently active advertisements.
  std::unordered_map<std::string, InstanceData> instances_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<LowEnergyPeripheralServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyPeripheralServer);
};

}  // namespace bthost
