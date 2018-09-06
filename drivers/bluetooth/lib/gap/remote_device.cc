// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/strings/string_printf.h"

#include "advertising_data.h"

namespace btlib {
namespace gap {
namespace {

std::string ConnectionStateToString(RemoteDevice::ConnectionState state) {
  switch (state) {
    case RemoteDevice::ConnectionState::kNotConnected:
      return "not connected";
    case RemoteDevice::ConnectionState::kInitializing:
      return "connecting";
    case RemoteDevice::ConnectionState::kConnected:
      return "connected";
    case RemoteDevice::ConnectionState::kBonding:
      return "bonding";
    case RemoteDevice::ConnectionState::kBonded:
      return "bonded";
  }

  ZX_PANIC("invalid connection state %u", static_cast<unsigned int>(state));
  return "(unknown)";
}

constexpr uint16_t kClockOffsetValidBitMask = 0x8000;

}  // namespace

RemoteDevice::RemoteDevice(DeviceCallback notify_listeners_callback,
                           DeviceCallback update_expiry_callback,
                           const std::string& identifier,
                           const common::DeviceAddress& address,
                           bool connectable)
    : notify_listeners_callback_(std::move(notify_listeners_callback)),
      update_expiry_callback_(std::move(update_expiry_callback)),
      identifier_(identifier),
      address_(address),
      technology_((address.type() == common::DeviceAddress::Type::kBREDR)
                      ? TechnologyType::kClassic
                      : TechnologyType::kLowEnergy),
      le_connection_state_(ConnectionState::kNotConnected),
      bredr_connection_state_(ConnectionState::kNotConnected),
      connectable_(connectable),
      temporary_(true),
      rssi_(hci::kRSSIInvalid),
      advertising_data_length_(0u) {
  ZX_DEBUG_ASSERT(notify_listeners_callback_);
  ZX_DEBUG_ASSERT(update_expiry_callback_);
  ZX_DEBUG_ASSERT(!identifier_.empty());
  // TODO(armansito): Add a mechanism for assigning "dual-mode" for technology.
}

void RemoteDevice::SetLEConnectionState(ConnectionState state) {
  ZX_DEBUG_ASSERT(connectable() || state == ConnectionState::kNotConnected);

  if (state == le_connection_state_) {
    bt_log(TRACE, "gap-le", "LE connection state already \"%s\"!",
           ConnectionStateToString(state).c_str());
    return;
  }

  bt_log(TRACE, "gap-le",
         "peer (%s) LE connection state changed from \"%s\" to \"%s\"",
         identifier_.c_str(),
         ConnectionStateToString(le_connection_state_).c_str(),
         ConnectionStateToString(state).c_str());

  le_connection_state_ = state;
  update_expiry_callback_(*this);
  notify_listeners_callback_(*this);
}

void RemoteDevice::SetBREDRConnectionState(ConnectionState state) {
  ZX_DEBUG_ASSERT(connectable() || state == ConnectionState::kNotConnected);

  if (state == bredr_connection_state_) {
    bt_log(TRACE, "gap-bredr", "BR/EDR connection state already \"%s\"",
           ConnectionStateToString(state).c_str());
    return;
  }

  bt_log(TRACE, "gap-bredr",
         "peer (%s) BR/EDR connection state changed from \"%s\" to \"%s\"",
         identifier_.c_str(),
         ConnectionStateToString(bredr_connection_state_).c_str(),
         ConnectionStateToString(state).c_str());

  bredr_connection_state_ = state;
  update_expiry_callback_(*this);
  notify_listeners_callback_(*this);
}

void RemoteDevice::SetLEAdvertisingData(
    int8_t rssi, const common::ByteBuffer& advertising_data) {
  ZX_DEBUG_ASSERT(technology() == TechnologyType::kLowEnergy);
  ZX_DEBUG_ASSERT(address_.type() != common::DeviceAddress::Type::kBREDR);
  update_expiry_callback_(*this);

  // Reallocate the advertising data buffer only if we need more space.
  // TODO(armansito): Revisit this strategy while addressing NET-209
  if (advertising_data_buffer_.size() < advertising_data.size()) {
    advertising_data_buffer_ =
        common::DynamicByteBuffer(advertising_data.size());
  }

  AdvertisingData old_parsed_ad;
  if (!AdvertisingData::FromBytes(advertising_data_buffer_, &old_parsed_ad)) {
    old_parsed_ad = AdvertisingData();
  }

  AdvertisingData new_parsed_ad;
  if (!AdvertisingData::FromBytes(advertising_data, &new_parsed_ad)) {
    new_parsed_ad = AdvertisingData();
  }

  rssi_ = rssi;
  advertising_data_length_ = advertising_data.size();
  advertising_data.Copy(&advertising_data_buffer_);

  if (old_parsed_ad.local_name() != new_parsed_ad.local_name()) {
    notify_listeners_callback_(*this);
  }
}

template <typename T>
typename std::enable_if<
    std::is_same<T, hci::InquiryResult>::value ||
    std::is_same<T, hci::InquiryResultRSSI>::value ||
    std::is_same<T, hci::ExtendedInquiryResultEventParams>::value>::type
RemoteDevice::SetInquiryData(const T& inquiry_result) {
  ZX_DEBUG_ASSERT(address_.value() == inquiry_result.bd_addr);
  update_expiry_callback_(*this);

  bool significant_change_common =
      !device_class_ || (device_class_->major_class() !=
                         inquiry_result.class_of_device.major_class());
  clock_offset_ =
      le16toh(kClockOffsetValidBitMask | inquiry_result.clock_offset);
  page_scan_repetition_mode_ = inquiry_result.page_scan_repetition_mode;
  device_class_ = inquiry_result.class_of_device;

  bool significant_change_specific = SetSpecificInquiryData(inquiry_result);
  if (significant_change_common || significant_change_specific) {
    notify_listeners_callback_(*this);
  }
}

template void RemoteDevice::SetInquiryData<>(const hci::InquiryResult&);
template void RemoteDevice::SetInquiryData<>(const hci::InquiryResultRSSI&);
template void RemoteDevice::SetInquiryData<>(
    const hci::ExtendedInquiryResultEventParams&);

void RemoteDevice::SetName(const std::string& name) {
  update_expiry_callback_(*this);
  if (!name_ || *name_ != name) {
    name_ = name;
    notify_listeners_callback_(*this);
  }
}

bool RemoteDevice::TryMakeNonTemporary() {
  // TODO(armansito): Since we don't currently support address resolution,
  // random addresses should never be persisted.
  if (!connectable() ||
      address().type() == common::DeviceAddress::Type::kLERandom ||
      address().type() == common::DeviceAddress::Type::kLEAnonymous) {
    bt_log(TRACE, "gap", "remains temporary: %s", ToString().c_str());
    return false;
  }

  if (temporary_) {
    temporary_ = false;
    update_expiry_callback_(*this);
    notify_listeners_callback_(*this);
  }

  return true;
}

std::string RemoteDevice::ToString() const {
  return fxl::StringPrintf("{remote-device id: %s, address: %s}",
                           identifier_.c_str(), address_.ToString().c_str());
}

// Private methods below.

bool RemoteDevice::SetExtendedInquiryResponse(const common::ByteBuffer& bytes) {
  ZX_DEBUG_ASSERT(bytes.size() <= hci::kExtendedInquiryResponseBytes);
  update_expiry_callback_(*this);

  if (extended_inquiry_response_.size() < bytes.size()) {
    extended_inquiry_response_ = common::DynamicByteBuffer(bytes.size());
  }
  bytes.Copy(&extended_inquiry_response_);

  // TODO(jamuraa): maybe rename this class?
  AdvertisingDataReader reader(extended_inquiry_response_);

  gap::DataType type;
  common::BufferView data;
  bool significant_change = false;
  while (reader.GetNextField(&type, &data)) {
    if (type == gap::DataType::kCompleteLocalName) {
      auto new_name = data.ToString();
      if (!name_ || *name_ != new_name) {
        name_ = new_name;
        significant_change = true;
      }
    }
  }

  return significant_change;
}

bool RemoteDevice::SetSpecificInquiryData(const hci::InquiryResult& result) {
  // All InquiryResult data is handled in the common case. Nothing left to do.
  return false;
}

bool RemoteDevice::SetSpecificInquiryData(
    const hci::InquiryResultRSSI& result) {
  rssi_ = result.rssi;
  return false;
}

bool RemoteDevice::SetSpecificInquiryData(
    const hci::ExtendedInquiryResultEventParams& result) {
  rssi_ = result.rssi;
  return SetExtendedInquiryResponse(common::BufferView(
      result.extended_inquiry_response, hci::kExtendedInquiryResponseBytes));
}

}  // namespace gap
}  // namespace btlib
