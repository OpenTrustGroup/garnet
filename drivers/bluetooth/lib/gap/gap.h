// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GAP_GAP_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GAP_GAP_H_

#include <cstdint>

// This file contains constants and numbers that are part of the Generic Access
// Profile specification.

namespace btlib {
namespace gap {

// Bluetooth technologies that a device can support.
enum class TechnologyType {
  kLowEnergy,
  kClassic,
  kDualMode,
};

enum class Mode {
  // Use the legacy HCI command set.
  kLegacy,

  // Use the extended HCI command set introduced in version 5.0
  kExtended,
};

// EIR Data Type, Advertising Data Type (AD Type), OOB Data Type definitions.
// clang-format off
enum class DataType : uint8_t {
  kFlags                        = 0x01,
  kIncomplete16BitServiceUuids  = 0x02,
  kComplete16BitServiceUuids    = 0x03,
  kIncomplete32BitServiceUuids  = 0x04,
  kComplete32BitServiceUuids    = 0x05,
  kIncomplete128BitServiceUuids = 0x06,
  kComplete128BitServiceUuids   = 0x07,
  kShortenedLocalName           = 0x08,
  kCompleteLocalName            = 0x09,
  kTxPowerLevel                 = 0x0A,
  kClassOfDevice                = 0x0D,
  kSSPOOBHash                   = 0x0E,
  kSSPOOBRandomizer             = 0x0F,
  kServiceData16Bit             = 0x16,
  kAppearance                   = 0x19,
  kServiceData32Bit             = 0x20,
  kServiceData128Bit            = 0x21,
  kURI                          = 0x24,
  kManufacturerSpecificData     = 0xFF,
  // TODO(armansito): Complete this list.
};
// clang-format on

// Constants for the expected size (in octets) of an
// advertising/EIR/scan-response data field.
//
//  * If a constant contains the word "Min", then it specifies a minimum
//    expected length rather than an exact length.
//
//  * If a constants contains the word "ElemSize", then the data field is
//    expected to contain a contiguous array of elements of the specified size.
constexpr size_t kAppearanceSize = 2;
constexpr size_t kManufacturerIdSize = 2;
constexpr size_t kTxPowerLevelSize = 1;

constexpr size_t kFlagsSizeMin = 1;
constexpr size_t kManufacturerSpecificDataSizeMin = kManufacturerIdSize;

constexpr size_t k16BitUuidElemSize = 2;
constexpr size_t k32BitUuidElemSize = 4;
constexpr size_t k128BitUuidElemSize = 16;

// Potential values that can be provided in the "Flags" advertising data
// bitfield.
// clang-format off
enum AdvFlag : uint8_t {
  // Octet 0
  kLELimitedDiscoverableMode        = (1 << 0),
  kLEGeneralDiscoverableMode        = (1 << 1),
  kBREDRNotSupported                = (1 << 2),
  kSimultaneousLEAndBREDRController = (1 << 3),
  kSimultaneousLEAndBREDRHost       = (1 << 4),
};
// clang-format on

// Constants used in Low Energy Discovery (see Core Spec v5.0, Vol 3, Part C,
// Appendix A).
constexpr int64_t kLEGeneralDiscoveryScanMinMs = 10240;       // 10.24 seconds
constexpr int64_t kLEGeneralDiscoveryScanMinCodedMs = 30720;  // 30.72 seconds
constexpr int64_t kLEScanFastPeriodMs = 30720;                // 30.72 seconds

// Recommended scan parameters that can be passed directly to the HCI commands.
// The HCI spec defines the time conversion as follows: Time =  N * 0.625 ms,
// where N is the value of the constant.
//
// A constant that contans the word "Coded" is recommended when using the LE
// Coded PHY. Otherwise the constant is recommended when using the LE 1M PHY.

// For user-initiated scanning
constexpr uint16_t kLEScanFastInterval = 0x0060;       // 60 ms
constexpr uint16_t kLEScanFastIntervalCoded = 0x0120;  // 180 ms
constexpr uint16_t kLEScanFastWindow = 0x0030;         // 30 ms
constexpr uint16_t kLEScanFastWindowCoded = 0x90;      // 90 ms

// For background scanning
constexpr uint16_t kLEScanSlowInterval1 = 0x0800;       // 1.28 s
constexpr uint16_t kLEScanSlowInterval1Coded = 0x1800;  // 3.84 s
constexpr uint16_t kLEScanSlowWindow1 = 0x0012;         // 11.25 ms
constexpr uint16_t kLEScanSlowWindow1Coded = 0x0036;    // 33.75 ms
constexpr uint16_t kLEScanSlowInterval2 = 0x1000;       // 2.56 s
constexpr uint16_t kLEScanSlowInterval2Coded = 0x3000;  // 7.68 s
constexpr uint16_t kLEScanSlowWindow2 = 0x0024;         // 22.5 ms
constexpr uint16_t kLEScanSlowWindow2Coded = 0x006C;    // 67.5 ms

// Timeout used for the LE Create Connection command.
constexpr int64_t kLECreateConnectionTimeoutMs = 20000;  // 20 s

// Connection Interval Timing Parameters (see v5.0, Vol 3, Part C,
// Section 9.3.12 and Appendix A)
constexpr int64_t kLEConnectionParameterTimeoutMs = 30000;  // 30 s
constexpr int64_t kLEConnectionPauseCentralMs = 1000;       // 1 s
constexpr int64_t kLEConnectionPausePeripheralMs = 5000;    // 5 s

constexpr uint16_t kLEInitialConnIntervalMin = 0x0018;       // 30 ms
constexpr uint16_t kLEInitialConnIntervalMax = 0x0028;       // 50 ms
constexpr uint16_t kLEInitialConnIntervalCodedMin = 0x0048;  // 90 ms
constexpr uint16_t kLEInitialConnIntervalCodedMax = 0x0078;  // 150 ms

}  // namespace gap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GAP_GAP_H_
