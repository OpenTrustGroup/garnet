// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/ap_mlme.h>

#include <fbl/ref_ptr.h>
#include <wlan/common/logging.h>
#include <wlan/protocol/mac.h>

namespace wlan {

ApMlme::ApMlme(DeviceInterface* device) : device_(device) {}

ApMlme::~ApMlme() {
    // Ensure the BSS is correctly stopped and terminated when destroying the MLME.
    if (bss_ != nullptr && bss_->IsStarted()) { bss_->Stop(); }
}

zx_status_t ApMlme::Init() {
    debugfn();
    return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
    debugfn();

    switch (id.target()) {
    case to_enum_type(ObjectTarget::kBss): {
        common::MacAddr client_addr(id.mac());
        return bss_->HandleTimeout(client_addr);
    }
    default:
        ZX_DEBUG_ASSERT(false);
        break;
    }

    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStartReq(const wlan_mlme::StartRequest& req) {
    debugfn();

    // Only one BSS can be started at a time.
    if (bss_ != nullptr) {
        debugf("BSS %s already running but received MLME-START.request\n",
               device_->GetState()->address().ToString().c_str());
        return ZX_OK;
    }

    // Configure BSS in driver.
    auto& bssid = device_->GetState()->address();
    wlan_bss_config_t cfg{
        .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
        .remote = false,
    };
    bssid.CopyTo(cfg.bssid);
    device_->ConfigureBss(&cfg);

    // Create and start BSS.
    auto bcn_sender = fbl::make_unique<BeaconSender>(device_);
    bss_ = fbl::AdoptRef(new InfraBss(device_, fbl::move(bcn_sender), bssid));
    bss_->Start(req);
    AddChildHandler(bss_);

    return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStopReq(const wlan_mlme::StopRequest& req) {
    debugfn();

    if (bss_ == nullptr) {
        errorf("received MLME-STOP.request but no BSS is running on device: %s\n",
               device_->GetState()->address().ToString().c_str());
        return ZX_OK;
    }

    // Stop and destroy BSS.
    RemoveChildHandler(bss_);
    bss_->Stop();
    bss_.reset();

    return ZX_OK;
}

zx_status_t ApMlme::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

zx_status_t ApMlme::PostChannelChange() {
    debugfn();
    // TODO(hahnr): Implement.
    return ZX_OK;
}

void ApMlme::HwIndication(uint32_t ind) {
    if (ind == WLAN_INDICATION_PRE_TBTT) {
        bss_->OnPreTbtt();
    } else if (ind == WLAN_INDICATION_BCN_TX_COMPLETE) {
        bss_->OnBcnTxComplete();
    }
}

}  // namespace wlan
