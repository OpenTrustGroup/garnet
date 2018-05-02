// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discovery_filter.h"

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"

namespace btlib {
namespace gap {
namespace {

bool MatchUuids(const std::vector<common::UUID>& uuids,
                const common::BufferView& data,
                size_t uuid_size) {
  if (data.size() % uuid_size) {
    FXL_LOG(WARNING) << "gap: DiscoveryFilter: Malformed service UUIDs list";
    return false;
  }

  size_t uuid_count = data.size() / uuid_size;
  for (size_t i = 0; i < uuid_count; i++) {
    const common::BufferView uuid_bytes(data.data() + i * uuid_size, uuid_size);
    for (const auto& uuid : uuids) {
      if (uuid.CompareBytes(uuid_bytes))
        return true;
    }
  }

  return false;
}

}  // namespace

void DiscoveryFilter::SetGeneralDiscoveryFlags() {
  set_flags(static_cast<uint8_t>(AdvFlag::kLEGeneralDiscoverableMode) |
            static_cast<uint8_t>(AdvFlag::kLELimitedDiscoverableMode));
}

bool DiscoveryFilter::MatchLowEnergyResult(
    const common::ByteBuffer& advertising_data,
    bool connectable,
    int8_t rssi) const {
  // No need to iterate over |advertising_data| for the |connectable_| filter.
  if (connectable_ && *connectable_ != connectable)
    return false;

  // If a pathloss filter is not set then apply the RSSI filter before iterating
  // over |advertising_data|. (An RSSI value of kRSSIInvalid means that RSSI is
  // not available, which we check for here).
  bool rssi_ok = !rssi_ || (rssi != hci::kRSSIInvalid && rssi >= *rssi_);
  if (!pathloss_ && !rssi_ok)
    return false;

  // Filters that require iterating over advertising data.
  bool flags_ok = !flags_;
  bool service_uuids_ok = service_uuids_.empty();
  bool name_ok = name_substring_.empty();
  bool manufacturer_ok = !manufacturer_code_;
  bool pathloss_ok = !pathloss_;
  bool tx_power_found = false;

  AdvertisingDataReader reader(advertising_data);
  if (advertising_data.size() && !reader.is_valid())
    return false;

  DataType type;
  common::BufferView data;
  while (reader.GetNextField(&type, &data)) {
    switch (type) {
      case DataType::kFlags: {
        if (flags_ok)
          break;

        // The Flags field may be zero or more octets long for potential future
        // extension. We only care about the first octet.
        if (data.size() < kFlagsSizeMin) {
          FXL_LOG(WARNING)
              << "gap: DiscoveryFilter: Malformed Flags field received";
          break;
        }

        // We check if all bits in |flags_| are present in the data.
        uint8_t masked_flags = data[0] & *flags_;
        flags_ok =
            all_flags_required_ ? (masked_flags == *flags_) : !!masked_flags;

        break;
      }
      case DataType::kTxPowerLevel: {
        if (pathloss_ok)
          break;

        if (data.size() != kTxPowerLevelSize) {
          FXL_LOG(WARNING)
              << "gap: DiscoveryFilter: Malformed Tx Power Level received";
          break;
        }

        tx_power_found = true;

        // An RSSI value of kRSSIInvalid means that RSSI is not available.
        if (rssi == hci::kRSSIInvalid)
          break;

        int8_t tx_power_lvl = static_cast<int8_t>(*data.data());
        if (tx_power_lvl < rssi) {
          FXL_LOG(WARNING) << "gap: DiscoveryFilter: Reported Tx Power Level "
                              "is less than the RSSI";
          break;
        }

        int8_t pathloss = tx_power_lvl - rssi;
        pathloss_ok = (pathloss <= *pathloss_);
        break;
      }
      case DataType::kCompleteLocalName:
      case DataType::kShortenedLocalName: {
        if (name_ok)
          break;

        auto name = data.AsString();
        name_ok = (name.find(name_substring_) != fxl::StringView::npos);
        break;
      }
      case DataType::kManufacturerSpecificData:
        if (manufacturer_ok)
          break;

        // The first two octets of the manufacturer specific data field contains
        // the Company Identifier Code.
        if (data.size() < kManufacturerSpecificDataSizeMin) {
          FXL_LOG(WARNING) << "gap: DiscoveryFilter: Malformed "
                              "manufacturer-specific data received";
          break;
        }

        manufacturer_ok =
            (le16toh(*reinterpret_cast<const uint16_t*>(data.data())) ==
             *manufacturer_code_);
        break;
      case DataType::kIncomplete16BitServiceUuids:
      case DataType::kComplete16BitServiceUuids:
        if (!service_uuids_ok) {
          service_uuids_ok =
              MatchUuids(service_uuids_, data, k16BitUuidElemSize);
        }
        break;
      case DataType::kIncomplete32BitServiceUuids:
      case DataType::kComplete32BitServiceUuids:
        if (!service_uuids_ok) {
          service_uuids_ok =
              MatchUuids(service_uuids_, data, k32BitUuidElemSize);
        }
        break;
      case DataType::kIncomplete128BitServiceUuids:
      case DataType::kComplete128BitServiceUuids:
        if (!service_uuids_ok) {
          service_uuids_ok =
              MatchUuids(service_uuids_, data, k128BitUuidElemSize);
        }
        break;
      default:
        break;
    }
  }

  // If the pathloss filter failed, then fall back to RSSI if requested.
  if (!pathloss_ok) {
    // No match if the Tx Power Level was provided and the computed pathloss
    // value was not within the threshold.
    if (tx_power_found)
      return false;

    // If no RSSI filter was set OR if one was set but it didn't match the scan
    // result, we fail.
    if (!rssi_ || !rssi_ok)
      return false;
  }

  return flags_ok && service_uuids_ok && name_ok && manufacturer_ok;
}

void DiscoveryFilter::Reset() {
  service_uuids_.clear();
  name_substring_.clear();
  connectable_.Reset();
  manufacturer_code_.Reset();
  pathloss_.Reset();
  rssi_.Reset();
}

}  // namespace gap
}  // namespace btlib
