// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_state.h"
#include "garnet/drivers/bluetooth/lib/hci/acl_data_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"
#include "garnet/drivers/bluetooth/lib/hci/lmp_feature_set.h"
#include "lib/fxl/logging.h"

namespace btlib {
namespace gap {

// Stores controller settings and state information.
class AdapterState final {
 public:
  AdapterState();

  // The HCI version supported by the controller.
  hci::HCIVersion hci_version() const { return hci_version_; }

  // This returns Bluetooth Controller address. This address has the following
  // meaning based on the controller capabilities:
  //   - On BR/EDR this is the Bluetooth Controller Address, or BD_ADDR.
  //   - On LE this is the Public Device Address. This value can be used as the
  //     device's identity address. This value can be zero if a Public Device
  //     Address is not used.
  //   - On BR/EDR/LE this is the LE Public Device Address AND the BD_ADDR.
  const common::DeviceAddressBytes& controller_address() const {
    return controller_address_;
  }

  // The features that are supported by this controller.
  const hci::LMPFeatureSet& features() const { return features_; }

  // Helpers for querying LMP capabilities.
  inline bool IsBREDRSupported() const {
    return !features().HasBit(0u, hci::LMPFeature::kBREDRNotSupported);
  }

  inline bool IsLowEnergySupported() const {
    return features().HasBit(0u, hci::LMPFeature::kLESupported);
  }

  // Returns true if |command_bit| in the given |octet| is set in the supported
  // command list.
  inline bool IsCommandSupported(size_t octet,
                                 hci::SupportedCommand command_bit) const {
    FXL_DCHECK(octet < sizeof(supported_commands_));
    return supported_commands_[octet] & static_cast<uint8_t>(command_bit);
  }

  // Returns Bluetooth Low Energy specific state information.
  const LowEnergyState& low_energy_state() const { return le_state_; }

  // Returns the BR/EDR ACL data buffer capacity.
  const hci::DataBufferInfo& bredr_data_buffer_info() const {
    return bredr_data_buffer_info_;
  }

 private:
  // Let Adapter directly write to the private members.
  friend class Adapter;

  // The member variables in this class consist of controller settings that are
  // shared between LE and BR/EDR controllers. LE and BR/EDR specific state is
  // stored in corresponding data structures.
  // TODO(armansito): Actually do this and update the comment to refer to the
  // variables.

  // HCI version supported by the controller.
  hci::HCIVersion hci_version_;

  // The Features that are supported by this adapter.
  hci::LMPFeatureSet features_;

  // Bitmask list of HCI commands that the controller supports.
  uint8_t supported_commands_[64];

  // BD_ADDR (for classic) and Public Device Address (for LE).
  common::DeviceAddressBytes controller_address_;

  // The BR/EDR ACL data buffer size. We store this here as it is needed on
  // dual-mode controllers even if the host stack is compiled for LE-only.
  hci::DataBufferInfo bredr_data_buffer_info_;

  // BLE-specific state.
  LowEnergyState le_state_;

  // TODO(armansito): Add BREDRState class.
};

}  // namespace gap
}  // namespace btlib
