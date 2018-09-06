// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_HCI_LEGACY_LOW_ENERGY_ADVERTISER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_HCI_LEGACY_LOW_ENERGY_ADVERTISER_H_

#include "garnet/drivers/bluetooth/lib/hci/low_energy_advertiser.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace btlib {
namespace hci {

class Transport;

class LegacyLowEnergyAdvertiser final : public LowEnergyAdvertiser {
 public:
  LegacyLowEnergyAdvertiser(fxl::RefPtr<Transport> hci);
  ~LegacyLowEnergyAdvertiser() override;

  // LowEnergyAdvertiser overrides:
  size_t GetSizeLimit() override;
  size_t GetMaxAdvertisements() const override { return 1; }

  // LegacyLowEnergyAdvertiser supports only a single advertising instance,
  // hence it can report additional errors in the following conditions:
  // 1. If called while a start request is pending, reports kRepeatedAttempts.
  // 2. If called while a stop request is pending, then cancels the stop request
  //    and proceeds with start.
  void StartAdvertising(const common::DeviceAddress& address,
                        const common::ByteBuffer& data,
                        const common::ByteBuffer& scan_rsp,
                        ConnectionCallback connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        AdvertisingStatusCallback callback) override;

  // If called while a stop request is pending, returns false.
  // If called while a start request is pending, then cancels the start
  // request and proceeds with start.
  // Returns false if called while not advertising.
  bool StopAdvertising(const common::DeviceAddress& address) override;

  // Clears the advertising state before passing |link| on to
  // |connect_callback_|.
  void OnIncomingConnection(ConnectionPtr link) override;

 private:
  // Unconditionally stops advertising.
  void StopAdvertisingInternal();

  // Returns true if currently advertising.
  bool advertising() const { return advertised_ != common::DeviceAddress(); }

  // The transport that's used to issue commands
  fxl::RefPtr<Transport> hci_;

  // |hci_cmd_runner_| will be running when a start or stop is pending.
  // |starting_| is set to true if a start is pending.
  bool starting_;
  std::unique_ptr<SequentialCommandRunner> hci_cmd_runner_;

  // Non-zero if advertising has been enabled.
  common::DeviceAddress advertised_;

  // if not null, the callback for connectable advertising.
  ConnectionCallback connect_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LegacyLowEnergyAdvertiser);
};

}  // namespace hci

}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_HCI_LEGACY_LOW_ENERGY_ADVERTISER_H_
