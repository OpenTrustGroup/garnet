// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/scanner.h>

#include "mock_device.h"

#include <wlan/mlme/clock.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <gtest/gtest.h>
#include <cstring>

#include <fuchsia/cpp/wlan_mlme.h>

namespace wlan {
namespace {

const uint8_t kBeacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x00, 0x09, 0x74, 0x65, 0x73, 0x74, 0x20, 0x73, 0x73, 0x69, 0x64,
};

class ScannerTest : public ::testing::Test {
   public:
    ScannerTest() : scanner_(&mock_dev_, mock_dev_.CreateTimer(1u)) { SetupMessages(); }

   protected:
    void SetupMessages() {
        req_ = wlan_mlme::ScanRequest::New();
        req_->channel_list.resize(0);
        req_->channel_list->push_back(1);
    }

    void SetPassive() { req_->scan_type = wlan_mlme::ScanTypes::PASSIVE; }

    void SetActive() { req_->scan_type = wlan_mlme::ScanTypes::ACTIVE; }

    zx_status_t Start() {
        return scanner_.Start(*req_);
    }

    zx_status_t DeserializeScanResponse() {
        return mock_dev_.GetQueuedServiceMsg(wlan_mlme::Method::SCAN_confirm, &resp_);
    }

    wlan_mlme::ScanRequestPtr req_;
    wlan_mlme::ScanConfirm resp_;
    MockDevice mock_dev_;
    Scanner scanner_;
};

TEST_F(ScannerTest, Start) {
    EXPECT_EQ(0u, mock_dev_.GetChannelNumber());
    EXPECT_FALSE(scanner_.IsRunning());

    EXPECT_EQ(ZX_OK, Start());
    EXPECT_TRUE(scanner_.IsRunning());

    EXPECT_EQ(1u, mock_dev_.GetChannelNumber());
}

TEST_F(ScannerTest, Start_InvalidChannelTimes) {
    req_->min_channel_time = 2;
    req_->max_channel_time = 1;

    EXPECT_EQ(0u, mock_dev_.GetChannelNumber());

    EXPECT_EQ(ZX_OK, Start());
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(0u, mock_dev_.GetChannelNumber());

    EXPECT_EQ(ZX_OK, DeserializeScanResponse());
    EXPECT_EQ(0u, resp_.bss_description_set->size());
    EXPECT_EQ(wlan_mlme::ScanResultCodes::NOT_SUPPORTED, resp_.result_code);
}

TEST_F(ScannerTest, Start_NoChannels) {
    SetupMessages();
    req_->channel_list.resize(0);

    EXPECT_EQ(0u, mock_dev_.GetChannelNumber());

    EXPECT_EQ(ZX_OK, Start());
    EXPECT_FALSE(scanner_.IsRunning());
    EXPECT_EQ(0u, mock_dev_.GetChannelNumber());

    EXPECT_EQ(ZX_OK, DeserializeScanResponse());
    EXPECT_EQ(0u, resp_.bss_description_set->size());
    EXPECT_EQ(wlan_mlme::ScanResultCodes::NOT_SUPPORTED, resp_.result_code);
}

TEST_F(ScannerTest, Reset) {
    ASSERT_EQ(ZX_OK, Start());
    ASSERT_TRUE(scanner_.IsRunning());

    scanner_.Reset();
    EXPECT_FALSE(scanner_.IsRunning());
    // TODO(tkilbourn): check all the other invariants
}

TEST_F(ScannerTest, PassiveScan) {
    SetPassive();

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(Scanner::Type::kPassive, scanner_.ScanType());
}

TEST_F(ScannerTest, ActiveScan) {
    SetActive();

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(Scanner::Type::kActive, scanner_.ScanType());
}

TEST_F(ScannerTest, ScanChannel) {
    ASSERT_EQ(ZX_OK, Start());
    auto chan = scanner_.ScanChannel();
    EXPECT_EQ(1u, chan.primary);
}

TEST_F(ScannerTest, Timeout_MinChannelTime) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(WLAN_TU(req_->min_channel_time).get(), scanner_.timer().deadline().get());

    mock_dev_.AdvanceTime(WLAN_TU(req_->min_channel_time));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());
    EXPECT_EQ(zx::time() + WLAN_TU(req_->max_channel_time), scanner_.timer().deadline());
}

TEST_F(ScannerTest, Timeout_MaxChannelTime) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;

    ASSERT_EQ(ZX_OK, Start());

    mock_dev_.AdvanceTime(WLAN_TU(req_->min_channel_time));
    ASSERT_EQ(ZX_OK, scanner_.HandleTimeout());

    mock_dev_.AdvanceTime(WLAN_TU(req_->max_channel_time));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());

    EXPECT_EQ(ZX_OK, DeserializeScanResponse());
    EXPECT_EQ(0u, resp_.bss_description_set->size());
    EXPECT_EQ(wlan_mlme::ScanResultCodes::SUCCESS, resp_.result_code);
}

TEST_F(ScannerTest, Timeout_NextChannel) {
    SetPassive();
    req_->min_channel_time = 1;
    req_->max_channel_time = 10;
    req_->channel_list.push_back(2);

    EXPECT_EQ(0u, mock_dev_.GetChannelNumber());

    ASSERT_EQ(ZX_OK, Start());
    ASSERT_EQ(1u, scanner_.ScanChannel().primary);

    EXPECT_EQ(1u, mock_dev_.GetChannelNumber());

    mock_dev_.AdvanceTime(WLAN_TU(req_->min_channel_time));
    ASSERT_EQ(ZX_OK, scanner_.HandleTimeout());

    mock_dev_.AdvanceTime(WLAN_TU(req_->max_channel_time));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());
    EXPECT_EQ(2u, scanner_.ScanChannel().primary);
    EXPECT_EQ(mock_dev_.GetTime() + WLAN_TU(req_->min_channel_time), scanner_.timer().deadline());

    EXPECT_EQ(2u, mock_dev_.GetChannelNumber());
}

TEST_F(ScannerTest, DISABLED_Timeout_ProbeDelay) {
    SetActive();
    req_->probe_delay = 1;
    req_->min_channel_time = 5;
    req_->max_channel_time = 10;

    ASSERT_EQ(ZX_OK, Start());
    EXPECT_EQ(WLAN_TU(req_->probe_delay).get(), scanner_.timer().deadline().get());

    mock_dev_.AdvanceTime(WLAN_TU(req_->probe_delay));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());
    EXPECT_EQ(WLAN_TU(req_->min_channel_time).get(), scanner_.timer().deadline().get());
}

TEST_F(ScannerTest, ScanResponse) {
    SetPassive();

    ASSERT_EQ(ZX_OK, Start());

    wlan_rx_info_t info;
    info.valid_fields = WLAN_RX_INFO_VALID_RSSI | WLAN_RX_INFO_VALID_SNR;
    info.chan = {
        .primary = 1,
    };
    info.rssi = 10;
    info.snr = 60;

    auto hdr = reinterpret_cast<const MgmtFrameHeader*>(kBeacon);
    auto bcn = reinterpret_cast<const Beacon*>(kBeacon + hdr->len());
    auto beacon = ImmutableMgmtFrame<Beacon>(hdr, bcn, sizeof(kBeacon) - hdr->len());

    EXPECT_EQ(ZX_OK, scanner_.HandleBeacon(beacon, info));
    mock_dev_.SetTime(zx::time(1));
    EXPECT_EQ(ZX_OK, scanner_.HandleTimeout());

    EXPECT_EQ(ZX_OK, DeserializeScanResponse());
    ASSERT_EQ(1u, resp_.bss_description_set->size());
    EXPECT_EQ(wlan_mlme::ScanResultCodes::SUCCESS, resp_.result_code);

    auto& bss = resp_.bss_description_set->at(0);
    EXPECT_EQ(0, std::memcmp(kBeacon + 16, bss.bssid.data(), 6));
    EXPECT_STREQ("test ssid", bss.ssid.get().c_str());
    EXPECT_EQ(wlan_mlme::BSSTypes::INFRASTRUCTURE, bss.bss_type);
    EXPECT_EQ(100u, bss.beacon_period);
    EXPECT_EQ(1024u, bss.timestamp);
    // EXPECT_EQ(1u, bss->channel);  // IE missing. info.chan != bss->channel.
    EXPECT_EQ(10u, bss.rssi_measurement);
    EXPECT_EQ(0, bss.rcpi_measurement);  // Not reported. Default at 0.
    EXPECT_EQ(60u, bss.rsni_measurement);
}

// TODO(hahnr): add test for active scanning

}  // namespace
}  // namespace wlan
