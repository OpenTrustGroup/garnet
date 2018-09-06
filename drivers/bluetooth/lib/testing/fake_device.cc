// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_device.h"

#include <endian.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_defs.h"

namespace btlib {

using common::ByteBuffer;

namespace testing {
namespace {

void WriteRandomRSSI(int8_t* out_mem) {
  constexpr int8_t kRSSIMin = -127;
  constexpr int8_t kRSSIMax = 20;

  int8_t rssi;
  zx_cprng_draw(reinterpret_cast<unsigned char*>(&rssi), sizeof(rssi));
  rssi = (rssi % (kRSSIMax - kRSSIMin)) + kRSSIMin;

  *out_mem = rssi;
}

}  // namespace

FakeDevice::FakeDevice(const common::DeviceAddress& address, bool connectable,
                       bool scannable)
    : ctrl_(nullptr),
      address_(address),
      connected_(false),
      connectable_(connectable),
      scannable_(scannable),
      connect_status_(hci::StatusCode::kSuccess),
      connect_response_(hci::StatusCode::kSuccess),
      force_pending_connect_(false),
      should_batch_reports_(false),
      gatt_server_(this) {}

void FakeDevice::SetAdvertisingData(const common::ByteBuffer& data) {
  ZX_DEBUG_ASSERT(data.size() <= hci::kMaxLEAdvertisingDataLength);
  adv_data_ = common::DynamicByteBuffer(data);
}

void FakeDevice::SetScanResponse(bool should_batch_reports,
                                 const common::ByteBuffer& data) {
  ZX_DEBUG_ASSERT(scannable_);
  ZX_DEBUG_ASSERT(data.size() <= hci::kMaxLEAdvertisingDataLength);
  scan_rsp_ = common::DynamicByteBuffer(data);
  should_batch_reports_ = should_batch_reports;
}

common::DynamicByteBuffer FakeDevice::CreateInquiryResponseEvent(
    hci::InquiryMode mode) const {
  ZX_DEBUG_ASSERT(address_.type() == common::DeviceAddress::Type::kBREDR);

  size_t param_size;
  if (mode == hci::InquiryMode::kStandard) {
    param_size =
        sizeof(hci::InquiryResultEventParams) + sizeof(hci::InquiryResult);
  } else {
    param_size = sizeof(hci::InquiryResultWithRSSIEventParams) +
                 sizeof(hci::InquiryResultRSSI);
  }

  common::DynamicByteBuffer buffer(sizeof(hci::EventHeader) + param_size);
  common::MutablePacketView<hci::EventHeader> event(&buffer, param_size);
  event.mutable_header()->parameter_total_size = param_size;

  // TODO(jamuraa): simultate clock offset and RSSI
  if (mode == hci::InquiryMode::kStandard) {
    event.mutable_header()->event_code = hci::kInquiryResultEventCode;
    auto payload = event.mutable_payload<hci::InquiryResultEventParams>();
    payload->num_responses = 1u;

    auto inq_result = reinterpret_cast<hci::InquiryResult*>(payload->responses);
    inq_result->bd_addr = address_.value();
    inq_result->page_scan_repetition_mode = hci::PageScanRepetitionMode::kR0;
    inq_result->class_of_device = class_of_device_;
    inq_result->clock_offset = 0;
  } else {
    event.mutable_header()->event_code = hci::kInquiryResultWithRSSIEventCode;
    auto payload =
        event.mutable_payload<hci::InquiryResultWithRSSIEventParams>();
    payload->num_responses = 1u;

    auto inq_result =
        reinterpret_cast<hci::InquiryResultRSSI*>(payload->responses);
    inq_result->bd_addr = address_.value();
    inq_result->page_scan_repetition_mode = hci::PageScanRepetitionMode::kR0;
    inq_result->class_of_device = class_of_device_;
    inq_result->clock_offset = 0;
    inq_result->rssi = -30;
  }

  return buffer;
}

common::DynamicByteBuffer FakeDevice::CreateAdvertisingReportEvent(
    bool include_scan_rsp) const {
  size_t param_size = sizeof(hci::LEMetaEventParams) +
                      sizeof(hci::LEAdvertisingReportSubeventParams) +
                      sizeof(hci::LEAdvertisingReportData) + adv_data_.size() +
                      sizeof(int8_t);
  if (include_scan_rsp) {
    ZX_DEBUG_ASSERT(scannable_);
    param_size += sizeof(hci::LEAdvertisingReportData) + scan_rsp_.size() +
                  sizeof(int8_t);
  }

  common::DynamicByteBuffer buffer(sizeof(hci::EventHeader) + param_size);
  common::MutablePacketView<hci::EventHeader> event(&buffer, param_size);
  event.mutable_header()->event_code = hci::kLEMetaEventCode;
  event.mutable_header()->parameter_total_size = param_size;

  auto payload = event.mutable_payload<hci::LEMetaEventParams>();
  payload->subevent_code = hci::kLEAdvertisingReportSubeventCode;

  auto subevent_payload =
      reinterpret_cast<hci::LEAdvertisingReportSubeventParams*>(
          payload->subevent_parameters);
  subevent_payload->num_reports = include_scan_rsp ? 2 : 1;

  auto report = reinterpret_cast<hci::LEAdvertisingReportData*>(
      subevent_payload->reports);
  if (connectable_) {
    report->event_type = hci::LEAdvertisingEventType::kAdvInd;
  } else if (scannable_) {
    report->event_type = hci::LEAdvertisingEventType::kAdvScanInd;
  } else {
    report->event_type = hci::LEAdvertisingEventType::kAdvNonConnInd;
  }

  // TODO(armansito): Use the resolved address types for <5.0 LE Privacy.
  report->address_type =
      (address_.type() == common::DeviceAddress::Type::kLERandom)
          ? hci::LEAddressType::kRandom
          : hci::LEAddressType::kPublic;
  report->address = address_.value();
  report->length_data = adv_data_.size();
  std::memcpy(report->data, adv_data_.data(), adv_data_.size());

  WriteRandomRSSI(
      reinterpret_cast<int8_t*>(report->data + report->length_data));

  if (include_scan_rsp) {
    WriteScanResponseReport(reinterpret_cast<hci::LEAdvertisingReportData*>(
        report->data + report->length_data + sizeof(int8_t)));
  }

  return buffer;
}

common::DynamicByteBuffer FakeDevice::CreateScanResponseReportEvent() const {
  ZX_DEBUG_ASSERT(scannable_);
  size_t param_size = sizeof(hci::LEMetaEventParams) +
                      sizeof(hci::LEAdvertisingReportSubeventParams) +
                      sizeof(hci::LEAdvertisingReportData) + scan_rsp_.size() +
                      sizeof(int8_t);

  common::DynamicByteBuffer buffer(sizeof(hci::EventHeader) + param_size);
  common::MutablePacketView<hci::EventHeader> event(&buffer, param_size);
  event.mutable_header()->event_code = hci::kLEMetaEventCode;
  event.mutable_header()->parameter_total_size = param_size;

  auto payload = event.mutable_payload<hci::LEMetaEventParams>();
  payload->subevent_code = hci::kLEAdvertisingReportSubeventCode;

  auto subevent_payload =
      reinterpret_cast<hci::LEAdvertisingReportSubeventParams*>(
          payload->subevent_parameters);
  subevent_payload->num_reports = 1;

  auto report = reinterpret_cast<hci::LEAdvertisingReportData*>(
      subevent_payload->reports);
  WriteScanResponseReport(report);

  return buffer;
}

void FakeDevice::AddLink(hci::ConnectionHandle handle) {
  ZX_DEBUG_ASSERT(!HasLink(handle));
  logical_links_.insert(handle);

  if (logical_links_.size() == 1u)
    set_connected(true);
}

void FakeDevice::RemoveLink(hci::ConnectionHandle handle) {
  ZX_DEBUG_ASSERT(HasLink(handle));
  logical_links_.erase(handle);
  if (logical_links_.empty())
    set_connected(false);
}

bool FakeDevice::HasLink(hci::ConnectionHandle handle) const {
  return logical_links_.count(handle) != 0u;
}

FakeDevice::HandleSet FakeDevice::Disconnect() {
  set_connected(false);
  return std::move(logical_links_);
}

void FakeDevice::WriteScanResponseReport(
    hci::LEAdvertisingReportData* report) const {
  ZX_DEBUG_ASSERT(scannable_);
  report->event_type = hci::LEAdvertisingEventType::kScanRsp;
  report->address_type =
      (address_.type() == common::DeviceAddress::Type::kLERandom)
          ? hci::LEAddressType::kRandom
          : hci::LEAddressType::kPublic;
  report->address = address_.value();
  report->length_data = scan_rsp_.size();
  std::memcpy(report->data, scan_rsp_.data(), scan_rsp_.size());

  WriteRandomRSSI(
      reinterpret_cast<int8_t*>(report->data + report->length_data));
}

void FakeDevice::OnRxL2CAP(hci::ConnectionHandle conn,
                           const common::ByteBuffer& pdu) {
  if (pdu.size() < sizeof(l2cap::BasicHeader)) {
    bt_log(WARN, "fake-hci", "malformed L2CAP packet!");
    return;
  }

  const auto& header = pdu.As<l2cap::BasicHeader>();
  uint16_t len = le16toh(header.length);
  l2cap::ChannelId chan_id = le16toh(header.channel_id);

  auto payload = pdu.view(sizeof(l2cap::BasicHeader));
  if (payload.size() != len) {
    bt_log(WARN, "fake-hci", "malformed L2CAP B-frame header!");
    return;
  }

  switch (chan_id) {
    case l2cap::kATTChannelId:
      gatt_server_.HandlePdu(conn, payload);
      break;

    // TODO(armansito): Support other channels
    default:
      break;
  }
}

}  // namespace testing
}  // namespace btlib
