// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GAP_LOW_ENERGY_STATE_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GAP_LOW_ENERGY_STATE_H_

#include <cstdint>

#include "garnet/drivers/bluetooth/lib/hci/acl_data_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

namespace btlib {
namespace gap {

// Stores Bluetooth Low Energy settings and state information.
class LowEnergyState final {
 public:
  LowEnergyState();

  // Returns true if |feature_bit| is set as supported in the local LE features
  // list.
  inline bool IsFeatureSupported(hci::LESupportedFeature feature_bit) const {
    return supported_features_ & static_cast<uint64_t>(feature_bit);
  }

  // Returns the LE ACL data buffer capacity.
  const hci::DataBufferInfo& data_buffer_info() const {
    return data_buffer_info_;
  }

 private:
  friend class Adapter;

  // Storage capacity information about the controller's internal ACL data
  // buffers.
  hci::DataBufferInfo data_buffer_info_;

  // Local supported LE Features reported by the controller.
  uint64_t supported_features_;

  // Local supported LE states reported by the controller.
  uint64_t supported_states_;
};

}  // namespace gap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GAP_LOW_ENERGY_STATE_H_
