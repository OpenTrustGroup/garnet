// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/beacon_sender_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/timer.h>

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <zircon/types.h>

#include <thread>

namespace wlan {

using timestamp_t = std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>;

// BeaconSender sends periodic Beacon frames for a given BSS. The BeaconSender only supports one
// BSS at a time. Beacons are sent from a separate thread.
// Sending Beacons through software is unlikely to be precise enough due to the tight time
// constraints. Use this BeaconSender only for development purposes to cut ties to the driver.
// In all other cases make use of the HwBeaconSender which requires the driver to pick-up the Beacon
// frame and correctly configure its hardware.
class BeaconSender : public BeaconSenderInterface {
   public:
    explicit BeaconSender(DeviceInterface* device);

    zx_status_t Init() override;
    bool IsStarted() override;
    zx_status_t Start(const StartRequest& req) override;
    zx_status_t Stop() override;

   private:
    bool IsStartedLocked() const;
    void MessageLoop();
    zx_status_t SendBeaconFrameLocked();
    zx_status_t SetTimeout();
    uint64_t beacon_timestamp();
    uint16_t next_seq();

    static constexpr uint64_t kMessageLoopMaxWaitSeconds = 30;
    // Indicates the packet was send due to the Timer being triggered.
    static constexpr uint64_t kPortPktKeyTimer = 1;
    // Message which will shut down the currently running Beacon thread.
    static constexpr uint64_t kPortPktKeyShutdown = 2;

    std::thread bcn_thread_;
    zx::port port_;
    DeviceInterface* const device_;
    fbl::unique_ptr<Timer> timer_;
    StartRequestPtr start_req_;
    std::mutex start_req_lock_;
    uint16_t last_seq_ = kMaxSequenceNumber;
    timestamp_t started_at_;
};

}  // namespace wlan
