// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GAP_LOW_ENERGY_ADVERTISING_MANAGER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GAP_LOW_ENERGY_ADVERTISING_MANAGER_H_

#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_advertiser.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {

namespace hci {
class Connection;
class Transport;
}  // namespace hci

namespace gap {

class LowEnergyAdvertisingManager {
 public:
  explicit LowEnergyAdvertisingManager(hci::LowEnergyAdvertiser* advertiser);
  virtual ~LowEnergyAdvertisingManager();

  // Asynchronously attempts to start advertising a set of |data| with
  // additional scan response data |scan_rsp|.
  // If |connect_callback| is provided, the advertisement will be connectable
  // and it will be called with the returned advertisement_id and a pointer to
  // the new connection, at which point the advertisement will have been
  // stopped.
  //
  // Returns false if the parameters represent an invalid advertisement:
  //  * if |anonymous| is true but |callback| is set
  //
  // |status_callback| may be called synchronously within this function.
  // |status_callback| provides one of:
  //  - an |advertisement_id|, which can be used to stop advertising
  //    or disambiguate calls to |callback|, and a success |status|.
  //  - an empty |advertisement_id| and an error indication in |status|:
  //    * common::HostError::kInvalidParameters if the advertising parameters
  //      are invalid (e.g. |data| is too large).
  //    * common::HostError::kNotSupported if another set cannot be advertised
  //      or if the requested parameters are not supported by the hardware.
  //    * common::HostError::kProtocolError with the a HCI error reported from
  //      the controller, otherwise.
  //
  // TODO(armansito): Return integer IDs instead.
  using ConnectionCallback =
      fit::function<void(std::string advertisement_id,
                         std::unique_ptr<hci::Connection> link)>;
  using AdvertisingStatusCallback =
      fit::function<void(std::string advertisement_id, hci::Status status)>;
  void StartAdvertising(const AdvertisingData& data,
                        const AdvertisingData& scan_rsp,
                        ConnectionCallback connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        AdvertisingStatusCallback status_callback);

  // Stop advertising the advertisement with the id |advertisement_id|
  // Returns true if an advertisement was stopped, and false otherwise.
  // This function is idempotent.
  bool StopAdvertising(std::string advertisement_id);

 private:
  class ActiveAdvertisement;

  // Active advertisements, indexed by id.
  // TODO(armansito): Use fbl::HashMap here (NET-176) or move
  // ActiveAdvertisement definition here and store by value (it is a small
  // object).
  std::unordered_map<std::string, std::unique_ptr<ActiveAdvertisement>>
      advertisements_;

  // Used to communicate with the controller. |advertiser_| must outlive this
  // advertising manager.
  hci::LowEnergyAdvertiser* advertiser_;  // weak

  // Note: Should remain the last member so it'll be destroyed and
  // invalidate it's pointers before other members are destroyed.
  fxl::WeakPtrFactory<LowEnergyAdvertisingManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyAdvertisingManager);
};

}  // namespace gap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GAP_LOW_ENERGY_ADVERTISING_MANAGER_H_
