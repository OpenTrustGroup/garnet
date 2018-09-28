// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/station.h>

#include <wlan/common/channel.h>
#include <wlan/common/energy.h>
#include <wlan/common/logging.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/sequence.h>
#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <zircon/status.h>

#include <inttypes.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace wlan {

namespace wlan_stats = ::fuchsia::wlan::stats;
using common::dBm;

// TODO(hahnr): Revisit frame construction to reduce boilerplate code.

Station::Station(DeviceInterface* device, TimerManager&& timer_mgr, ChannelScheduler* chan_sched)
    : device_(device), timer_mgr_(std::move(timer_mgr)), chan_sched_(chan_sched) {
    bssid_.Reset();
}

void Station::Reset() {
    debugfn();

    state_ = WlanState::kUnjoined;
    bss_.reset();
    join_timeout_.Cancel();
    auth_timeout_.Cancel();
    assoc_timeout_.Cancel();
    signal_report_timeout_.Cancel();
    bssid_.Reset();
    bu_queue_.clear();
}
zx_status_t Station::HandleAnyMlmeMsg(const BaseMlmeMsg& mlme_msg) {
    WLAN_STATS_INC(svc_msg.in);

    // Always process MLME-JOIN.requests.
    if (auto join_req = mlme_msg.As<wlan_mlme::JoinRequest>()) {
        return HandleMlmeJoinReq(*join_req);
    }

    // Drop other MLME requests if there is no BSSID set yet.
    if (bssid() == nullptr) { return ZX_OK; }

    if (auto auth_req = mlme_msg.As<wlan_mlme::AuthenticateRequest>()) {
        return HandleMlmeAuthReq(*auth_req);
    } else if (auto deauth_req = mlme_msg.As<wlan_mlme::DeauthenticateRequest>()) {
        return HandleMlmeDeauthReq(*deauth_req);
    } else if (auto assoc_req = mlme_msg.As<wlan_mlme::AssociateRequest>()) {
        return HandleMlmeAssocReq(*assoc_req);
    } else if (auto eapol_req = mlme_msg.As<wlan_mlme::EapolRequest>()) {
        return HandleMlmeEapolReq(*eapol_req);
    } else if (auto setkeys_req = mlme_msg.As<wlan_mlme::SetKeysRequest>()) {
        return HandleMlmeSetKeysReq(*setkeys_req);
    }
    return ZX_OK;
}

zx_status_t Station::HandleAnyWlanFrame(fbl::unique_ptr<Packet> pkt) {
    ZX_DEBUG_ASSERT(pkt->peer() == Packet::Peer::kWlan);
    WLAN_STATS_INC(rx_frame.in);
    WLAN_STATS_ADD(pkt->len(), rx_frame.in_bytes);

    if (auto possible_mgmt_frame = MgmtFrameView<>::CheckType(pkt.get())) {
        auto mgmt_frame = possible_mgmt_frame.CheckLength();
        if (!mgmt_frame) { return ZX_ERR_BUFFER_TOO_SMALL; }

        HandleAnyMgmtFrame(mgmt_frame.IntoOwned(fbl::move(pkt)));
    } else if (auto possible_data_frame = DataFrameView<>::CheckType(pkt.get())) {
        auto data_frame = possible_data_frame.CheckLength();
        if (!data_frame) { return ZX_ERR_BUFFER_TOO_SMALL; }

        HandleAnyDataFrame(data_frame.IntoOwned(fbl::move(pkt)));
    }

    return ZX_OK;
}

zx_status_t Station::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    auto mgmt_frame = frame.View();

    WLAN_STATS_INC(mgmt_frame.in);
    if (ShouldDropMgmtFrame(mgmt_frame)) {
        WLAN_STATS_INC(mgmt_frame.drop);
        return ZX_ERR_NOT_SUPPORTED;
    }
    WLAN_STATS_INC(mgmt_frame.out);

    if (auto possible_bcn_frame = mgmt_frame.CheckBodyType<Beacon>()) {
        if (auto bcn_frame = possible_bcn_frame.CheckLength()) {
            HandleBeacon(bcn_frame.IntoOwned(frame.Take()));
        }
    } else if (auto possible_auth_frame = mgmt_frame.CheckBodyType<Authentication>()) {
        if (auto auth_frame = possible_auth_frame.CheckLength()) {
            HandleAuthentication(auth_frame.IntoOwned(frame.Take()));
        }
    } else if (auto possible_deauth_frame = mgmt_frame.CheckBodyType<Deauthentication>()) {
        if (auto deauth_frame = possible_deauth_frame.CheckLength()) {
            HandleDeauthentication(deauth_frame.IntoOwned(frame.Take()));
        }
    } else if (auto possible_assoc_resp_frame = mgmt_frame.CheckBodyType<AssociationResponse>()) {
        if (auto assoc_resp_frame = possible_assoc_resp_frame.CheckLength()) {
            HandleAssociationResponse(assoc_resp_frame.IntoOwned(frame.Take()));
        }
    } else if (auto possible_disassoc_frame = mgmt_frame.CheckBodyType<Disassociation>()) {
        if (auto disassoc_frame = possible_disassoc_frame.CheckLength()) {
            HandleDisassociation(disassoc_frame.IntoOwned(frame.Take()));
        }
    } else if (auto possible_action_frame = mgmt_frame.CheckBodyType<ActionFrame>()) {
        if (auto action_frame = possible_action_frame.CheckLength()) {
            HandleActionFrame(action_frame.IntoOwned(frame.Take()));
        }
    }

    return ZX_OK;
}

zx_status_t Station::HandleAnyDataFrame(DataFrame<>&& frame) {
    auto data_frame = frame.View();
    if (kFinspectEnabled) { DumpDataFrame(data_frame); }

    WLAN_STATS_INC(data_frame.in);
    if (ShouldDropDataFrame(data_frame)) { return ZX_ERR_NOT_SUPPORTED; }

    auto rssi_dbm = frame.View().rx_info()->rssi_dbm;
    WLAN_RSSI_HIST_INC(assoc_data_rssi, rssi_dbm);

    if (auto amsdu_frame = data_frame.CheckBodyType<AmsduSubframeHeader>().CheckLength()) {
        HandleAmsduFrame(amsdu_frame.IntoOwned(frame.Take()));
    } else if (auto llc_frame = data_frame.CheckBodyType<LlcHeader>().CheckLength()) {
        HandleDataFrame(llc_frame.IntoOwned(frame.Take()));
    } else if (auto null_frame = data_frame.CheckBodyType<NullDataHdr>().CheckLength()) {
        HandleNullDataFrame(null_frame.IntoOwned(frame.Take()));
    }

    return ZX_OK;
}

zx_status_t Station::HandleMlmeJoinReq(const MlmeMsg<wlan_mlme::JoinRequest>& req) {
    debugfn();

    if (state_ != WlanState::kUnjoined) {
        warnf("already joined; resetting station\n");
        Reset();
    }

    // Clone request to take ownership of the BSS.
    bss_ = wlan_mlme::BSSDescription::New();
    req.body()->selected_bss.Clone(bss_.get());
    bssid_.Set(bss_->bssid.data());

    auto chan = GetBssChan();
    infof("JoinReq BSSID %s Chan %u CBW %u Sec80 %u Phy %u\n",
          common::MacAddr(bssid_).ToString().c_str(), chan.primary, chan.cbw, chan.secondary80,
          req.body()->phy);

    if (!common::IsValidChan(chan)) {
        // If what SME instructs seems invalid, treat it as an error.
        // Shout out, and auto-correct
        wlan_channel_t chan_sanitized = chan;
        chan_sanitized.cbw = common::GetValidCbw(chan);
        errorf("Wlanstack attempts to configure an invalid channel: %s. Falling back to %s\n",
               common::ChanStr(chan).c_str(), common::ChanStr(chan_sanitized).c_str());
        chan = chan_sanitized;
    }

    debugjoin("setting channel to %s\n", common::ChanStr(chan).c_str());
    zx_status_t status = chan_sched_->SetChannel(chan);

    if (status != ZX_OK) {
        errorf("could not set wlan channel to %s (status %d)\n", common::ChanStr(chan).c_str(),
               status);
        Reset();
        service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    // Stay on channel to make sure we don't miss the beacon
    chan_sched_->EnsureOnChannel(timer_mgr_.Now() + kOnChannelTimeAfterSend);

    join_chan_ = chan;
    join_phy_ = req.body()->phy;
    zx::time deadline = deadline_after_bcn_period(req.body()->join_failure_timeout);
    status = timer_mgr_.Schedule(deadline, &join_timeout_);
    if (status != ZX_OK) {
        errorf("could not set join timeout event: %d\n", status);
        Reset();
        service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    // TODO(hahnr): Update when other BSS types are supported.
    wlan_bss_config_t cfg{
        .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
        .remote = true,
    };
    bssid_.CopyTo(cfg.bssid);
    device_->ConfigureBss(&cfg);
    return status;
}

zx_status_t Station::HandleMlmeAuthReq(const MlmeMsg<wlan_mlme::AuthenticateRequest>& req) {
    debugfn();

    if (bss_ == nullptr) { return ZX_ERR_BAD_STATE; }
    if (state_ != WlanState::kJoined) {
        errorf("received AUTHENTICATE.request in wrong state: %u\n", state_);
        return service::SendAuthConfirm(device_, bssid_,
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    // TODO(tkilbourn): better result codes
    common::MacAddr peer_sta_addr(req.body()->peer_sta_address.data());
    if (bssid_ != peer_sta_addr) {
        errorf("received authentication request for unknown BSS: %s, expected BSS: %s\n",
               peer_sta_addr.ToString().c_str(), bssid_.ToString().c_str());
        return service::SendAuthConfirm(device_, bssid_,
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    if (req.body()->auth_type != wlan_mlme::AuthenticationTypes::OPEN_SYSTEM) {
        errorf("only OpenSystem authentication is supported\n");
        return service::SendAuthConfirm(device_, bssid_,
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    debugjoin("authenticating to %s\n", bssid_.ToString().c_str());

    MgmtFrame<Authentication> frame;
    auto status = CreateMgmtFrame(&frame);
    if (status != ZX_OK) {
        errorf("authenticating: failed to build authentication frame: %s\n",
               zx_status_get_string(status));
        return status;
    }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid_;
    hdr->addr2 = self_addr();
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    frame.FillTxInfo();

    // This assumes Open System authentication.
    auto auth = frame.body();
    auth->auth_algorithm_number = auth_alg_;
    auth->auth_txn_seq_number = 1;
    auth->status_code = 0;  // Reserved: explicitly set to 0

    zx::time deadline = deadline_after_bcn_period(req.body()->auth_failure_timeout);
    status = timer_mgr_.Schedule(deadline, &auth_timeout_);
    if (status != ZX_OK) {
        errorf("could not set authentication timeout event: %s\n", zx_status_get_string(status));
        // This is the wrong result code, but we need to define our own codes at some later time.
        service::SendAuthConfirm(device_, bssid_, wlan_mlme::AuthenticateResultCodes::REFUSED);
        return status;
    }

    finspect("Outbound Mgmt Frame(Auth): %s\n", debug::Describe(*hdr).c_str());
    status = SendNonData(frame.Take());
    if (status != ZX_OK) {
        errorf("could not send authentication frame: %d\n", status);
        service::SendAuthConfirm(device_, bssid_, wlan_mlme::AuthenticateResultCodes::REFUSED);
        return status;
    }

    state_ = WlanState::kAuthenticating;
    return status;
}

zx_status_t Station::HandleMlmeDeauthReq(const MlmeMsg<wlan_mlme::DeauthenticateRequest>& req) {
    debugfn();

    if (state_ != WlanState::kAssociated && state_ != WlanState::kAuthenticated) {
        errorf("not associated or authenticated; ignoring deauthenticate request\n");
        return ZX_OK;
    }

    if (bss_ == nullptr) { return ZX_ERR_BAD_STATE; }

    // Check whether the request wants to deauthenticate from this STA's BSS.
    common::MacAddr peer_sta_addr(req.body()->peer_sta_address.data());
    if (bssid_ != peer_sta_addr) { return ZX_OK; }

    auto status = SendDeauthFrame(req.body()->reason_code);
    if (status != ZX_OK) {
        errorf("could not send deauth packet: %d\n", status);
        // Deauthenticate nevertheless. IEEE isn't clear on what we are supposed to do.
    }

    infof("deauthenticating from \"%s\" (%s), reason=%hu\n",
          debug::ToAsciiOrHexStr(*bss_->ssid).c_str(), bssid_.ToString().c_str(),
          req.body()->reason_code);

    state_ = WlanState::kJoined;
    device_->SetStatus(0);
    controlled_port_ = eapol::PortState::kBlocked;
    bu_queue_.clear();
    service::SendDeauthConfirm(device_, bssid_);

    return ZX_OK;
}

zx_status_t Station::HandleMlmeAssocReq(const MlmeMsg<wlan_mlme::AssociateRequest>& req) {
    debugfn();

    if (bss_ == nullptr) { return ZX_ERR_BAD_STATE; }

    // TODO(tkilbourn): better result codes
    common::MacAddr peer_sta_addr(req.body()->peer_sta_address.data());
    if (bssid_ != peer_sta_addr) {
        errorf("bad peer STA address for association\n");
        return service::SendAuthConfirm(device_, bssid_,
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kUnjoined || state_ == WlanState::kJoined) {
        errorf("must authenticate before associating\n");
        return service::SendAuthConfirm(device_, bssid_,
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kAssociated) {
        warnf("already authenticated; sending request anyway\n");
    }

    debugjoin("associating to %s\n", MACSTR(bssid_));

    size_t reserved_ie_len = 128;
    MgmtFrame<AssociationRequest> frame;
    auto status = CreateMgmtFrame(&frame, reserved_ie_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid_;
    hdr->addr2 = self_addr();
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    frame.FillTxInfo();

    auto assoc = frame.body();

    auto ifc_info = device_->GetWlanInfo().ifc_info;
    auto client_capability = ToAssocContext(ifc_info, join_chan_);

    assoc->cap = OverrideCapability(client_capability.cap);
    assoc->listen_interval = 0;

    ElementWriter w(assoc->elements, reserved_ie_len);
    if (!w.write<SsidElement>(bss_->ssid->data(), bss_->ssid->size())) {
        errorf("could not write ssid \"%s\" to association request\n",
               debug::ToAsciiOrHexStr(*bss_->ssid).c_str());
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    auto supp_rates = client_capability.supported_rates;
    if (!w.write<SupportedRatesElement>(std::move(supp_rates))) {
        errorf("could not write supported rates\n");
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    auto ext_rates = client_capability.ext_supported_rates;
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    // Write RSNE from MLME-Association.request if available.
    if (req.body()->rsn) {
        if (!w.write<RsnElement>(req.body()->rsn->data(), req.body()->rsn->size())) {
            return ZX_ERR_IO;
        }
    }

    if (IsHtOrLater()) {
        auto ht_cap = client_capability.ht_cap;
        debugf("HT cap(hardware reports): %s\n", debug::Describe(ht_cap).c_str());

        zx_status_t status = OverrideHtCapability(&ht_cap);
        if (status != ZX_OK) {
            errorf("could not build HtCapabilities. status %d\n", status);
            service::SendAssocConfirm(device_,
                                      wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
            return ZX_ERR_IO;
        }
        debugf("HT cap(after overriding): %s\n", debug::Describe(ht_cap).c_str());

        if (!w.write<HtCapabilities>(ht_cap.ht_cap_info, ht_cap.ampdu_params, ht_cap.mcs_set,
                                     ht_cap.ht_ext_cap, ht_cap.txbf_cap, ht_cap.asel_cap)) {
            errorf("could not write HtCapabilities\n");
            service::SendAssocConfirm(device_,
                                      wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
            return ZX_ERR_IO;
        }
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(assoc->Validate(w.size()));

    size_t body_len = frame.body()->len() + w.size();
    status = frame.set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("could not set body length to %zu: %d\n", body_len, status);
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    finspect("Outbound Mgmt Frame (AssocReq): %s\n", debug::Describe(*hdr).c_str());
    status = SendNonData(frame.Take());
    if (status != ZX_OK) {
        errorf("could not send assoc packet: %d\n", status);
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    // TODO(NET-500): Add association timeout to MLME-ASSOCIATE.request just like
    // JOIN and AUTHENTICATE requests do.
    zx::time deadline = deadline_after_bcn_period(kAssocBcnCountTimeout);
    status = timer_mgr_.Schedule(deadline, &assoc_timeout_);
    if (status != ZX_OK) {
        errorf("could not set auth timedout event: %d\n", status);
        // This is the wrong result code, but we need to define our own codes at some later time.
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        // TODO(tkilbourn): reset the station?
    }
    return status;
}

bool Station::ShouldDropMgmtFrame(const MgmtFrameView<>& frame) {
    // Drop management frames if either, there is no BSSID set yet,
    // or the frame is not from the BSS.
    return bssid() == nullptr || *bssid() != frame.hdr()->addr3;
}

// TODO(NET-500): Using a single method for joining and associated state is not ideal.
// The logic should be split up and decided on a higher level based on the current state.
zx_status_t Station::HandleBeacon(MgmtFrame<Beacon>&& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(bss_ != nullptr);

    auto rssi_dbm = frame.View().rx_info()->rssi_dbm;
    avg_rssi_dbm_.add(dBm(rssi_dbm));

    WLAN_RSSI_HIST_INC(beacon_rssi, rssi_dbm);

    // TODO(tkilbourn): update any other info (like rolling average of rssi)
    if (join_timeout_.IsActive()) {
        join_timeout_.Cancel();

        state_ = WlanState::kJoined;
        debugjoin("joined \"%s\"\n", debug::ToAsciiOrHexStr(*bss_->ssid).c_str());
        return service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::SUCCESS);
    }

    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    remaining_auto_deauth_timeout_ = FullAutoDeauthDuration();
    auto_deauth_last_accounted_ = timer_mgr_.Now();

    size_t elt_len = frame.body_len() - frame.body()->len();
    ElementReader reader(frame.body()->elements, elt_len);
    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) break;

        switch (hdr->id) {
        case element_id::kTim: {
            auto tim = reader.read<TimElement>();
            if (tim == nullptr) goto done_iter;
            if (tim->traffic_buffered(assoc_ctx_.aid)) { SendPsPoll(); }
            break;
        }
        default:
            reader.skip(sizeof(ElementHeader) + hdr->len);
            break;
        }
    }

done_iter:
    return ZX_OK;
}

zx_status_t Station::HandleAuthentication(MgmtFrame<Authentication>&& frame) {
    debugfn();

    if (state_ != WlanState::kAuthenticating) {
        debugjoin("unexpected authentication frame in state: %u; ignoring frame\n", state_);
        return ZX_OK;
    }

    // Authentication notification received. Cancel pending timeout.
    auth_timeout_.Cancel();

    auto auth = frame.body();
    if (auth->auth_algorithm_number != auth_alg_) {
        errorf("mismatched authentication algorithm (expected %u, got %u)\n", auth_alg_,
               auth->auth_algorithm_number);
        state_ = WlanState::kJoined;
        service::SendAuthConfirm(device_, bssid_,
                                 wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_INVALID_ARGS;
    }

    // This assumes Open System authentication.
    if (auth->auth_txn_seq_number != 2) {
        errorf("unexpected auth txn sequence number (expected 2, got %u)\n",
               auth->auth_txn_seq_number);
        state_ = WlanState::kJoined;
        service::SendAuthConfirm(device_, bssid_,
                                 wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_INVALID_ARGS;
    }

    if (auth->status_code != status_code::kSuccess) {
        errorf("authentication failed (status code=%u)\n", auth->status_code);
        state_ = WlanState::kJoined;
        service::SendAuthConfirm(device_, bssid_,
                                 wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_BAD_STATE;
    }

    state_ = WlanState::kAuthenticated;
    debugjoin("authenticated to %s\n", bssid_.ToString().c_str());
    service::SendAuthConfirm(device_, bssid_, wlan_mlme::AuthenticateResultCodes::SUCCESS);
    return ZX_OK;
}

zx_status_t Station::HandleDeauthentication(MgmtFrame<Deauthentication>&& frame) {
    debugfn();

    if (state_ != WlanState::kAssociated && state_ != WlanState::kAuthenticated) {
        debugjoin("got spurious deauthenticate; ignoring\n");
        return ZX_OK;
    }

    auto deauth = frame.body();
    infof("deauthenticating from \"%s\" (%s), reason=%hu\n",
          debug::ToAsciiOrHexStr(*bss_->ssid).c_str(), bssid_.ToString().c_str(),
          deauth->reason_code);

    state_ = WlanState::kJoined;
    device_->SetStatus(0);
    controlled_port_ = eapol::PortState::kBlocked;
    bu_queue_.clear();

    return service::SendDeauthIndication(device_, bssid_,
                                         static_cast<wlan_mlme::ReasonCode>(deauth->reason_code));
}

zx_status_t Station::HandleAssociationResponse(MgmtFrame<AssociationResponse>&& frame) {
    debugfn();

    if (state_ != WlanState::kAuthenticated) {
        // TODO(tkilbourn): should we process this Association response packet anyway? The spec is
        // unclear.
        debugjoin("unexpected association response frame\n");
        return ZX_OK;
    }

    // Receive association response, cancel association timeout.
    assoc_timeout_.Cancel();

    auto assoc = frame.body();
    if (assoc->status_code != status_code::kSuccess) {
        errorf("association failed (status code=%u)\n", assoc->status_code);
        // TODO(tkilbourn): map to the correct result code
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_BAD_STATE;
    }

    auto status = SetAssocContext(frame.View());
    if (status != ZX_OK) {
        errorf("failed to set association context (status %d)\n", status);
        service::SendAssocConfirm(device_,
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_BAD_STATE;
    }

    // TODO(porce): Move into |assoc_ctx_|
    common::MacAddr bssid(bss_->bssid.data());
    state_ = WlanState::kAssociated;
    assoc_ctx_.set_aid(assoc->aid);

    // Spread the good news upward
    service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::SUCCESS, assoc_ctx_.aid);
    // Spread the good news downward
    NotifyAssocContext();

    // Initiate RSSI reporting to Wlanstack.
    zx::time deadline = deadline_after_bcn_period(kSignalReportBcnCountTimeout);
    timer_mgr_.Schedule(deadline, &signal_report_timeout_);
    avg_rssi_dbm_.reset();
    avg_rssi_dbm_.add(dBm(frame.View().rx_info()->rssi_dbm));
    service::SendSignalReportIndication(device_, common::dBm(frame.View().rx_info()->rssi_dbm));

    remaining_auto_deauth_timeout_ = FullAutoDeauthDuration();
    status = timer_mgr_.Schedule(timer_mgr_.Now() + remaining_auto_deauth_timeout_,
                                 &auto_deauth_timeout_);
    if (status != ZX_OK) { warnf("could not set auto-deauthentication timeout event\n"); }

    // Open port if user connected to an open network.
    if (bss_->rsn.is_null()) {
        debugjoin("802.1X controlled port is now open\n");
        controlled_port_ = eapol::PortState::kOpen;
        device_->SetStatus(ETH_STATUS_ONLINE);
    }

    infof("NIC %s associated with \"%s\"(%s) in channel %s, %s, %s\n",
          self_addr().ToString().c_str(), debug::ToAsciiOrHexStr(*bss_->ssid).c_str(),
          bssid.ToString().c_str(), common::ChanStr(GetJoinChan()).c_str(),
          common::BandStr(GetJoinChan()).c_str(), GetPhyStr().c_str());

    // TODO(porce): Time when to establish BlockAck session
    // Handle MLME-level retry, if MAC-level retry ultimately fails
    // Wrap this as EstablishBlockAckSession(peer_mac_addr)
    // Signal to lower MAC for proper session handling

    if (IsHtOrLater()) { SendAddBaRequestFrame(); }
    return ZX_OK;
}

zx_status_t Station::HandleDisassociation(MgmtFrame<Disassociation>&& frame) {
    debugfn();

    if (state_ != WlanState::kAssociated) {
        debugjoin("got spurious disassociate; ignoring\n");
        return ZX_OK;
    }

    auto disassoc = frame.body();
    common::MacAddr bssid(bss_->bssid.data());
    infof("disassociating from \"%s\"(%s), reason=%u\n",
          debug::ToAsciiOrHexStr(*bss_->ssid).c_str(), MACSTR(bssid), disassoc->reason_code);

    state_ = WlanState::kAuthenticated;
    device_->SetStatus(0);
    controlled_port_ = eapol::PortState::kBlocked;
    signal_report_timeout_.Cancel();
    bu_queue_.clear();

    return service::SendDisassociateIndication(device_, bssid, disassoc->reason_code);
}

zx_status_t Station::HandleActionFrame(MgmtFrame<ActionFrame>&& frame) {
    debugfn();

    auto action_frame = frame.View().NextFrame();
    if (auto action_ba_frame = action_frame.CheckBodyType<ActionFrameBlockAck>().CheckLength()) {
        auto ba_frame = action_ba_frame.NextFrame();
        if (auto add_ba_resp_frame = ba_frame.CheckBodyType<AddBaResponseFrame>().CheckLength()) {
            finspect("Inbound ADDBA Resp frame: len %zu\n", add_ba_resp_frame.body_len());
            finspect("  addba resp: %s\n", debug::Describe(*add_ba_resp_frame.body()).c_str());

            // TODO(porce): Handle AddBaResponses and keep the result of negotiation.
        } else if (auto add_ba_req_frame =
                       ba_frame.CheckBodyType<AddBaRequestFrame>().CheckLength()) {
            finspect("Inbound ADDBA Req frame: len %zu\n", add_ba_req_frame.body_len());
            finspect("  addba req: %s\n", debug::Describe(*add_ba_req_frame.body()).c_str());

            return HandleAddBaRequest(*add_ba_req_frame.body());
        }
    }

    return ZX_OK;
}

zx_status_t Station::HandleAddBaRequest(const AddBaRequestFrame& addbareq) {
    debugfn();

    // Construct AddBaResponse frame
    MgmtFrame<ActionFrame> frame;
    size_t body_payload_len = ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
    auto status = CreateMgmtFrame(&frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid_;
    hdr->addr2 = self_addr();
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    frame.FillTxInfo();

    auto action_frame = frame.body();
    action_frame->category = ActionFrameBlockAck::ActionCategory();

    auto ba_frame = frame.NextFrame<ActionFrameBlockAck>();
    ba_frame.hdr()->action = AddBaResponseFrame::BlockAckAction();

    auto addbaresp_frame = ba_frame.NextFrame<AddBaResponseFrame>();
    auto addbaresp = addbaresp_frame.hdr();
    addbaresp->dialog_token = addbareq.dialog_token;

    // TODO(porce): Implement DelBa as a response to AddBar for decline

    // Note: Returning AddBaResponse with status_code::kRefused seems ineffective.
    // ArubaAP is persistent not honoring that.
    addbaresp->status_code = status_code::kSuccess;

    addbaresp->params.set_amsdu(addbareq.params.amsdu() == 1);
    addbaresp->params.set_policy(BlockAckParameters::kImmediate);
    addbaresp->params.set_tid(addbareq.params.tid());

    // TODO(porce): Once chipset capability is ready, refactor below buffer_size
    // calculation.
    auto buffer_size_ap = addbareq.params.buffer_size();
    constexpr size_t buffer_size_ralink = 64;
    auto buffer_size = (buffer_size_ap <= buffer_size_ralink) ? buffer_size_ap : buffer_size_ralink;
    addbaresp->params.set_buffer_size(buffer_size);
    addbaresp->timeout = addbareq.timeout;

    finspect("Outbound ADDBA Resp frame: len %zu\n", addbaresp_frame.len());
    finspect("Outbound Mgmt Frame(ADDBA Resp): %s\n", debug::Describe(*addbaresp).c_str());

    status = SendNonData(addbaresp_frame.Take());
    if (status != ZX_OK) {
        errorf("could not send AddBaResponse: %d\n", status);
        return status;
    }

    return ZX_OK;
}

bool Station::ShouldDropDataFrame(const DataFrameView<>& frame) {
    if (state_ != WlanState::kAssociated) { return true; }

    return bssid() == nullptr || *bssid() != frame.hdr()->addr2;
}

zx_status_t Station::HandleNullDataFrame(DataFrame<NullDataHdr>&& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(bssid() != nullptr);
    ZX_DEBUG_ASSERT(state_ == WlanState::kAssociated);

    // Take signal strength into account.
    avg_rssi_dbm_.add(dBm(frame.View().rx_info()->rssi_dbm));

    // Some AP's such as Netgear Routers send periodic NULL data frames to test whether a client
    // timed out. The client must respond with a NULL data frame itself to not get
    // deauthenticated.
    SendKeepAliveResponse();
    return ZX_OK;
}

zx_status_t Station::HandleDataFrame(DataFrame<LlcHeader>&& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(bssid() != nullptr);
    ZX_DEBUG_ASSERT(state_ == WlanState::kAssociated);

    auto data_llc_frame = frame.View();
    auto data_hdr = data_llc_frame.hdr();

    // Take signal strength into account.
    avg_rssi_dbm_.add(dBm(frame.View().rx_info()->rssi_dbm));

    // Forward EAPOL frames to SME.
    auto llc_frame = data_llc_frame.SkipHeader();
    if (auto eapol_frame = llc_frame.CheckBodyType<EapolHdr>().CheckLength().SkipHeader()) {
        if (eapol_frame.body_len() == eapol_frame.hdr()->get_packet_body_length()) {
            return service::SendEapolIndication(device_, *eapol_frame.hdr(), data_hdr->addr3,
                                                data_hdr->addr1);
        } else {
            errorf("received invalid EAPOL frame\n");
        }
        return ZX_OK;
    }

    // Drop packets if RSNA was not yet established.
    if (controlled_port_ == eapol::PortState::kBlocked) { return ZX_OK; }

    // PS-POLL if there are more buffered unicast frames.
    if (data_hdr->fc.more_data() && data_hdr->addr1.IsUcast()) { SendPsPoll(); }

    const auto& src = data_hdr->addr3;
    const auto& dest = data_hdr->addr1;
    size_t llc_payload_len = llc_frame.body_len();
    return HandleLlcFrame(llc_frame, llc_payload_len, src, dest);
}

zx_status_t Station::HandleLlcFrame(const FrameView<LlcHeader>& llc_frame, size_t llc_payload_len,
                                    const common::MacAddr& src, const common::MacAddr& dest) {
    finspect("Inbound LLC frame: hdr len %zu, payload len: %zu\n", llc_frame.hdr()->len(),
             llc_payload_len);
    finspect("  llc hdr: %s\n", debug::Describe(*llc_frame.hdr()).c_str());
    finspect("  llc payload: %s\n",
             debug::HexDump(llc_frame.body()->data, llc_payload_len).c_str());
    if (llc_payload_len == 0) {
        finspect("  dropping empty LLC frame\n");
        return ZX_OK;
    }

    // Prepare a packet
    const size_t eth_frame_len = EthernetII::max_len() + llc_payload_len;
    auto packet = GetEthPacket(eth_frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    EthFrame eth_frame(fbl::move(packet));
    auto eth_hdr = eth_frame.hdr();
    eth_hdr->dest = dest;
    eth_hdr->src = src;
    eth_hdr->ether_type = llc_frame.hdr()->protocol_id;
    std::memcpy(eth_frame.body()->data, llc_frame.body()->data, llc_payload_len);

    auto status = device_->SendEthernet(eth_frame.Take());
    if (status != ZX_OK) { errorf("could not send ethernet data: %d\n", status); }
    return status;
}

zx_status_t Station::HandleAmsduFrame(DataFrame<AmsduSubframeHeader>&& frame) {
    // TODO(porce): Define A-MSDU or MSDU signature, and avoid forceful conversion.
    debugfn();
    auto data_amsdu_frame = frame.View();

    // Non-DMG stations use basic subframe format only.
    if (data_amsdu_frame.body_len() == 0) { return ZX_OK; }
    finspect("Inbound AMSDU: len %zu\n", data_amsdu_frame.body_len());

    // TODO(porce): The received AMSDU should not be greater than max_amsdu_len, specified in
    // HtCapabilities IE of Association. Warn or discard if violated.

    const auto& src = data_amsdu_frame.hdr()->addr3;
    const auto& dest = data_amsdu_frame.hdr()->addr1;
    DeaggregateAmsdu(data_amsdu_frame, [&](FrameView<LlcHeader> llc_frame, size_t payload_len) {
        HandleLlcFrame(llc_frame, payload_len, src, dest);
    });

    return ZX_OK;
}

zx_status_t Station::HandleEthFrame(EthFrame&& eth_frame) {
    debugfn();

    // Drop Ethernet frames when not associated.
    auto bss_setup = (bssid() != nullptr);
    auto associated = (state_ == WlanState::kAssociated);
    if (!associated) { debugf("dropping eth packet while not associated\n"); }
    if (!bss_setup || !associated) { return ZX_OK; }

    // If off channel, buffer Ethernet frame
    if (!chan_sched_->OnChannel()) {
        if (bu_queue_.size() >= kMaxPowerSavingQueueSize) {
            bu_queue_.Dequeue();
            warnf("dropping oldest unicast frame\n");
        }
        bu_queue_.Enqueue(eth_frame.Take());
        debugps("queued frame since off channel; bu queue size: %lu\n", bu_queue_.size());
        return ZX_OK;
    }

    auto eth_hdr = eth_frame.hdr();
    const size_t frame_len =
        DataFrameHeader::max_len() + LlcHeader::max_len() + eth_frame.body_len();
    auto packet = GetWlanPacket(frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    bool needs_protection = !bss_->rsn.is_null() && controlled_port_ == eapol::PortState::kOpen;
    DataFrame<LlcHeader> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();

    // Set header
    bool has_ht_ctrl = false;
    std::memset(data_hdr, 0, DataFrameHeader::max_len());
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(IsQosReady() ? DataSubtype::kQosdata : DataSubtype::kDataSubtype);
    data_hdr->fc.set_to_ds(1);
    data_hdr->fc.set_from_ds(0);
    data_hdr->fc.set_htc_order(has_ht_ctrl ? 1 : 0);
    data_hdr->addr1 = common::MacAddr(bss_->bssid.data());
    data_hdr->addr2 = eth_hdr->src;
    data_hdr->addr3 = eth_hdr->dest;
    data_hdr->fc.set_protected_frame(needs_protection);

    // TODO(porce): Construct addr4 field

    // Ralink appears to setup BlockAck session AND AMPDU handling
    // TODO(porce): Use a separate sequence number space in that case
    if (assoc_ctx_.is_ht) {
        if (assoc_ctx_.is_cbw40_tx && data_hdr->addr3.IsUcast()) {
            // 40 MHz direction does not matter here.
            // Radio uses the operational channel setting. This indicates the bandwidth without
            // direction.
            data_frame.FillTxInfo(CBW40, WLAN_PHY_HT);
        } else {
            data_frame.FillTxInfo(CBW20, WLAN_PHY_HT);
        }
    } else {
        data_frame.FillTxInfo(CBW20, WLAN_PHY_OFDM);
    }

    if (data_hdr->HasQosCtrl()) {  // QoS Control field
        auto qos_ctrl = data_hdr->qos_ctrl();
        qos_ctrl->set_tid(GetTid(eth_frame));
        qos_ctrl->set_eosp(0);
        qos_ctrl->set_ack_policy(ack_policy::kNormalAck);

        // AMSDU: set_amsdu_present(1) requires dot11HighthroughputOptionImplemented should be true.
        qos_ctrl->set_amsdu_present(0);
        qos_ctrl->set_byte(0);
    }

    // TODO(porce): Construct htc_order field

    SetSeqNo(data_hdr, &seq_);

    auto llc_hdr = data_frame.body();
    llc_hdr->dsap = kLlcSnapExtension;
    llc_hdr->ssap = kLlcSnapExtension;
    llc_hdr->control = kLlcUnnumberedInformation;
    std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
    llc_hdr->protocol_id = eth_hdr->ether_type;
    std::memcpy(llc_hdr->payload, eth_hdr->payload, eth_frame.body_len());

    size_t actual_body_len = llc_hdr->len() + eth_frame.body_len();
    auto status = data_frame.set_body_len(actual_body_len);
    if (status != ZX_OK) {
        errorf("could not set data frame's body length to %zu: %d\n", actual_body_len, status);
        return status;
    }

    finspect("Outbound data frame: len %zu, hdr_len:%zu body_len:%zu\n", data_frame.len(),
             data_frame.hdr()->len(), data_frame.body_len());
    finspect("  wlan hdr: %s\n", debug::Describe(*data_frame.hdr()).c_str());
    finspect("  llc  hdr: %s\n", debug::Describe(*data_frame.body()).c_str());

    packet = data_frame.Take();
    finspect("  frame   : %s\n", debug::HexDump(packet->data(), packet->len()).c_str());

    status = SendWlan(fbl::move(packet));
    if (status != ZX_OK) { errorf("could not send wlan data: %d\n", status); }
    return status;
}

zx_status_t Station::HandleTimeout() {
    debugfn();
    zx::time now = timer_mgr_.HandleTimeout();

    if (join_timeout_.Triggered(now)) {
        debugjoin("join timed out; resetting\n");
        join_timeout_.Cancel();
        Reset();
        service::SendJoinConfirm(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
    } else if (auth_timeout_.Triggered(now)) {
        debugjoin("auth timed out; moving back to joined state\n");
        auth_timeout_.Cancel();
        state_ = WlanState::kJoined;
        service::SendAuthConfirm(device_, bssid_,
                                 wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
    } else if (assoc_timeout_.Triggered(now)) {
        debugjoin("assoc timed out; moving back to authenticated\n");
        assoc_timeout_.Cancel();
        // TODO(tkilbourn): need a better error code for this
        service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY);
    }

    if (signal_report_timeout_.Triggered(now)) {
        signal_report_timeout_.Cancel();

        if (state_ == WlanState::kAssociated) {
            service::SendSignalReportIndication(device_, common::to_dBm(avg_rssi_dbm_.avg()));

            zx::time deadline = deadline_after_bcn_period(kSignalReportBcnCountTimeout);
            timer_mgr_.Schedule(deadline, &signal_report_timeout_);
        }
    }

    if (auto_deauth_timeout_.Triggered(now)) {
        auto_deauth_timeout_.Cancel();

        debugclt("now: %lu\n", now.get());
        debugclt("remaining auto-deauth timeout: %lu\n", remaining_auto_deauth_timeout_.get());
        debugclt("auto-deauth last accounted time: %lu\n", auto_deauth_last_accounted_.get());

        if (!chan_sched_->OnChannel()) {
            ZX_DEBUG_ASSERT("auto-deauth timeout should not trigger while off channel\n");
        } else if (remaining_auto_deauth_timeout_ > now - auto_deauth_last_accounted_) {
            // Update the remaining auto-deauth timeout with the unaccounted time
            remaining_auto_deauth_timeout_ -= now - auto_deauth_last_accounted_;
            auto_deauth_last_accounted_ = now;
            timer_mgr_.Schedule(now + remaining_auto_deauth_timeout_, &auto_deauth_timeout_);
        } else if (state_ == WlanState::kAssociated) {
            infof("lost BSS; deauthenticating...\n");
            state_ = WlanState::kUnjoined;
            device_->SetStatus(0);
            controlled_port_ = eapol::PortState::kBlocked;

            auto reason_code = wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH;
            service::SendDeauthIndication(device_, bssid_, reason_code);
            auto status = SendDeauthFrame(reason_code);
            if (status != ZX_OK) { errorf("could not send deauth packet: %d\n", status); }
        }
    }

    return ZX_OK;
}

zx_status_t Station::SendKeepAliveResponse() {
    if (state_ != WlanState::kAssociated) {
        warnf("cannot send keep alive response before being associated\n");
        return ZX_OK;
    }

    auto packet = GetWlanPacket(DataFrameHeader::max_len());
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    packet->clear();

    DataFrame<> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kNull);
    data_hdr->fc.set_to_ds(1);
    data_hdr->addr1 = bssid_;
    data_hdr->addr2 = self_addr();
    data_hdr->addr3 = bssid_;
    SetSeqNo(data_hdr, &seq_);

    // Ralink appears to setup BlockAck session AND AMPDU handling
    // TODO(porce): Use a separate sequence number space in that case
    if (assoc_ctx_.is_cbw40_tx && data_hdr->addr3.IsUcast()) {
        // 40 MHz direction does not matter here.
        // Radio uses the operational channel setting. This indicates the bandwidth without
        // direction.
        data_frame.FillTxInfo(CBW40, WLAN_PHY_HT);
    } else {
        data_frame.FillTxInfo(CBW20, WLAN_PHY_HT);
    }

    // Adjust frame's length before sending it over the air.
    auto status = data_frame.set_body_len(0);
    if (status != ZX_OK) {
        errorf("could not adjust keep alive frame's length; hdr len: %zu; %d\n", data_hdr->len(),
               status);
        return status;
    }

    status = SendWlan(data_frame.Take());
    if (status != ZX_OK) {
        errorf("could not send keep alive frame: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t Station::SendAddBaRequestFrame() {
    debugfn();

    if (state_ != WlanState::kAssociated) {
        errorf("won't send ADDBA Request in other than Associated state. Current state: %d\n",
               state_);
        return ZX_ERR_BAD_STATE;
    }

    MgmtFrame<ActionFrame> frame;
    size_t body_payload_len = ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
    auto status = CreateMgmtFrame(&frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid_;
    hdr->addr2 = self_addr();
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    frame.FillTxInfo();

    auto action_hdr = frame.body();
    action_hdr->category = ActionFrameBlockAck::ActionCategory();

    auto ba_frame = frame.NextFrame<ActionFrameBlockAck>();
    ba_frame.hdr()->action = AddBaRequestFrame::BlockAckAction();

    auto addbareq_frame = ba_frame.NextFrame<AddBaRequestFrame>();
    auto addbareq = addbareq_frame.hdr();

    // It appears there is no particular rule to choose the value for
    // dialog_token. See IEEE Std 802.11-2016, 9.6.5.2.
    addbareq->dialog_token = 0x01;
    addbareq->params.set_amsdu(1);
    addbareq->params.set_policy(BlockAckParameters::BlockAckPolicy::kImmediate);
    addbareq->params.set_tid(GetTid());  // TODO(porce): Communicate this with lower MAC.
    // TODO(porce): Fix the discrepancy of this value from the Ralink's TXWI ba_win_size setting
    addbareq->params.set_buffer_size(64);
    addbareq->timeout = 0;               // Disables the timeout
    addbareq->seq_ctrl.set_fragment(0);  // TODO(porce): Send this down to the lower MAC
    addbareq->seq_ctrl.set_starting_seq(1);

    finspect("Outbound ADDBA Req frame: len %zu\n", addbareq_frame.len());
    finspect("  addba req: %s\n", debug::Describe(*addbareq).c_str());

    status = SendNonData(addbareq_frame.Take());
    if (status != ZX_OK) {
        errorf("could not send AddBaRequest: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t Station::HandleMlmeEapolReq(const MlmeMsg<wlan_mlme::EapolRequest>& req) {
    debugfn();

    if (!bss_) { return ZX_ERR_BAD_STATE; }
    if (state_ != WlanState::kAssociated) {
        debugf("dropping MLME-EAPOL.request while not being associated. STA in state %d\n", state_);
        return ZX_OK;
    }

    size_t llc_payload_len = req.body()->data->size();
    size_t max_frame_len = DataFrameHeader::max_len() + LlcHeader::max_len() + llc_payload_len;
    auto packet = GetWlanPacket(max_frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    packet->clear();

    bool needs_protection = !bss_->rsn.is_null() && controlled_port_ == eapol::PortState::kOpen;
    DataFrame<LlcHeader> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_to_ds(1);
    data_hdr->fc.set_protected_frame(needs_protection);
    data_hdr->addr1.Set(req.body()->dst_addr.data());
    data_hdr->addr2.Set(req.body()->src_addr.data());
    data_hdr->addr3.Set(req.body()->dst_addr.data());
    SetSeqNo(data_hdr, &seq_);

    auto llc_hdr = data_frame.body();
    llc_hdr->dsap = kLlcSnapExtension;
    llc_hdr->ssap = kLlcSnapExtension;
    llc_hdr->control = kLlcUnnumberedInformation;
    std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
    llc_hdr->protocol_id = htobe16(kEapolProtocolId);
    std::memcpy(llc_hdr->payload, req.body()->data->data(), llc_payload_len);

    // Adjust frame's length before sending it over the air.
    data_frame.set_body_len(llc_hdr->len() + llc_payload_len);
    data_frame.FillTxInfo(CBW20, WLAN_PHY_HT);

    zx_status_t status = SendWlan(data_frame.Take());
    if (status != ZX_OK) {
        errorf("could not send eapol request packet: %d\n", status);
        service::SendEapolConfirm(device_, wlan_mlme::EapolResultCodes::TRANSMISSION_FAILURE);
        return status;
    }

    service::SendEapolConfirm(device_, wlan_mlme::EapolResultCodes::SUCCESS);

    return status;
}

zx_status_t Station::HandleMlmeSetKeysReq(const MlmeMsg<wlan_mlme::SetKeysRequest>& req) {
    debugfn();

    for (auto& keyDesc : *req.body()->keylist) {
        if (keyDesc.key.is_null()) { return ZX_ERR_NOT_SUPPORTED; }

        uint8_t key_type;
        switch (keyDesc.key_type) {
        case wlan_mlme::KeyType::PAIRWISE:
            key_type = WLAN_KEY_TYPE_PAIRWISE;
            break;
        case wlan_mlme::KeyType::PEER_KEY:
            key_type = WLAN_KEY_TYPE_PEER;
            break;
        case wlan_mlme::KeyType::IGTK:
            key_type = WLAN_KEY_TYPE_IGTK;
            break;
        default:
            key_type = WLAN_KEY_TYPE_GROUP;
            break;
        }

        wlan_key_config_t key_config = {};
        memcpy(key_config.key, keyDesc.key->data(), keyDesc.key->size());
        key_config.key_type = key_type;
        key_config.key_len = static_cast<uint8_t>(keyDesc.key->size());
        key_config.key_idx = keyDesc.key_id;
        key_config.protection = WLAN_PROTECTION_RX_TX;
        key_config.cipher_type = keyDesc.cipher_suite_type;
        memcpy(key_config.cipher_oui, keyDesc.cipher_suite_oui.data(),
               sizeof(key_config.cipher_oui));
        memcpy(key_config.peer_addr, keyDesc.address.data(), sizeof(key_config.peer_addr));

        auto status = device_->SetKey(&key_config);
        if (status != ZX_OK) {
            errorf("Could not configure keys in hardware: %d\n", status);
            return status;
        }
    }

    // Once keys have been successfully configured, open controlled port and report link up
    // status.
    // TODO(hahnr): This is a very simplified assumption and we might need a little more logic to
    // correctly track the port's state.
    controlled_port_ = eapol::PortState::kOpen;
    device_->SetStatus(ETH_STATUS_ONLINE);
    return ZX_OK;
}

void Station::PreSwitchOffChannel() {
    debugfn();
    if (state_ == WlanState::kAssociated) {
        SetPowerManagementMode(true);

        auto_deauth_timeout_.Cancel();
        zx::duration unaccounted_time = timer_mgr_.Now() - auto_deauth_last_accounted_;
        if (remaining_auto_deauth_timeout_ > unaccounted_time) {
            remaining_auto_deauth_timeout_ -= unaccounted_time;
        } else {
            remaining_auto_deauth_timeout_ = zx::duration(0);
        }
    }
}

void Station::BackToMainChannel() {
    debugfn();
    if (state_ == WlanState::kAssociated) {
        SetPowerManagementMode(false);

        zx::time now = timer_mgr_.Now();
        auto deadline = now + std::max(remaining_auto_deauth_timeout_, WLAN_TU(1u));
        timer_mgr_.Schedule(deadline, &auto_deauth_timeout_);
        auto_deauth_last_accounted_ = now;

        SendBufferedUnits();
    }
}

void Station::SendBufferedUnits() {
    while (bu_queue_.size() > 0) {
        fbl::unique_ptr<Packet> packet = bu_queue_.Dequeue();
        debugps("sending buffered frame; queue size at: %lu\n", bu_queue_.size());
        ZX_DEBUG_ASSERT(packet->peer() == Packet::Peer::kEthernet);
        HandleEthFrame(EthFrame(fbl::move(packet)));
    }
}

void Station::DumpDataFrame(const DataFrameView<>& frame) {
    // TODO(porce): Should change the API signature to MSDU
    auto hdr = frame.hdr();

    auto is_ucast_to_self = self_addr() == hdr->addr1;
    auto is_mcast = hdr->addr1.IsBcast();
    auto is_bcast = hdr->addr1.IsMcast();
    auto is_interesting = is_ucast_to_self || is_mcast || is_bcast;

    auto associated = (state_ == WlanState::kAssociated);
    auto from_bss = (bssid() != nullptr && *bssid() == hdr->addr2);
    if (associated) { is_interesting = is_interesting && from_bss; }

    if (!is_interesting) { return; }

    auto msdu = frame.body()->data;
    finspect("Inbound data frame: len %zu\n", frame.len());
    finspect("  wlan hdr: %s\n", debug::Describe(*hdr).c_str());
    finspect("  msdu    : %s\n", debug::HexDump(msdu, frame.body_len()).c_str());
}

zx_status_t Station::SendNonData(fbl::unique_ptr<Packet> packet) {
    chan_sched_->EnsureOnChannel(timer_mgr_.Now() + kOnChannelTimeAfterSend);
    return SendWlan(fbl::move(packet));
}

zx_status_t Station::SetPowerManagementMode(bool ps_mode) {
    if (state_ != WlanState::kAssociated) {
        warnf("cannot adjust power management before being associated\n");
        return ZX_OK;
    }

    auto packet = GetWlanPacket(DataFrameHeader::max_len());
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    packet->clear();

    DataFrame<> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kNull);
    data_hdr->fc.set_pwr_mgmt(ps_mode);
    data_hdr->fc.set_to_ds(1);
    data_hdr->addr1 = bssid_;
    data_hdr->addr2 = self_addr();
    data_hdr->addr3 = bssid_;
    SetSeqNo(data_hdr, &seq_);

    // Ralink appears to setup BlockAck session AND AMPDU handling
    // TODO(porce): Use a separate sequence number space in that case
    if (assoc_ctx_.is_cbw40_tx && data_hdr->addr3.IsUcast()) {
        // 40 MHz direction does not matter here.
        // Radio uses the operational channel setting. This indicates the bandwidth without
        // direction.
        data_frame.FillTxInfo(CBW40, WLAN_PHY_HT);
    } else {
        data_frame.FillTxInfo(CBW20, WLAN_PHY_HT);
    }

    // Adjust frame's length before sending it over the air.
    auto status = data_frame.set_body_len(0);
    if (status != ZX_OK) {
        errorf("could not adjust power management frame's length; hdr len: %zu; %d\n",
               data_hdr->len(), status);
        return status;
    }

    status = SendWlan(data_frame.Take());
    if (status != ZX_OK) {
        errorf("could not send power management frame: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t Station::SendPsPoll() {
    // TODO(hahnr): We should probably wait for an RSNA if the network is an
    // RSN. Else we cannot work with the incoming data frame.
    if (state_ != WlanState::kAssociated) {
        warnf("cannot send ps-poll before being associated\n");
        return ZX_OK;
    }

    size_t len = CtrlFrameHdr::max_len() + PsPollFrame::max_len();
    auto packet = GetWlanPacket(len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    packet->clear();

    CtrlFrame<PsPollFrame> frame(std::move(packet));
    ZX_DEBUG_ASSERT(frame.HasValidLen());
    frame.hdr()->fc.set_type(FrameType::kControl);
    frame.hdr()->fc.set_subtype(ControlSubtype::kPsPoll);
    frame.body()->aid = assoc_ctx_.aid;
    frame.body()->bssid = common::MacAddr(bss_->bssid.data());
    frame.body()->ta = self_addr();

    zx_status_t status = SendNonData(frame.Take());
    if (status != ZX_OK) {
        errorf("could not send power management packet: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t Station::SendDeauthFrame(wlan_mlme::ReasonCode reason_code) {
    debugfn();

    MgmtFrame<Deauthentication> frame;
    auto status = CreateMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = bssid_;
    hdr->addr2 = self_addr();
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    frame.FillTxInfo();

    auto deauth = frame.body();
    deauth->reason_code = static_cast<uint16_t>(reason_code);

    finspect("Outbound Mgmt Frame(Deauth): %s\n", debug::Describe(*hdr).c_str());
    return SendNonData(frame.Take());
}

zx_status_t Station::SendWlan(fbl::unique_ptr<Packet> packet) {
    auto packet_bytes = packet->len();
    zx_status_t status = device_->SendWlan(fbl::move(packet));
    if (status == ZX_OK) {
        WLAN_STATS_INC(tx_frame.out);
        WLAN_STATS_ADD(packet_bytes, tx_frame.out_bytes);
    }
    return status;
}

zx::time Station::deadline_after_bcn_period(size_t bcn_count) {
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    return timer_mgr_.Now() + WLAN_TU(bss_->beacon_period * bcn_count);
}

zx::duration Station::FullAutoDeauthDuration() {
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    return WLAN_TU(bss_->beacon_period * kAutoDeauthBcnCountTimeout);
}

bool Station::IsCbw40Rx() const {
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    if (bss_ == nullptr) { return false; }

    auto chan = GetBssChan();
    auto ifc_info = device_->GetWlanInfo().ifc_info;
    auto client_assoc = ToAssocContext(ifc_info, chan);

    debugf(
        "IsCbw40Rx: bss.ht_cap:%s, bss.chan_width_set:%s client_assoc.has_ht_cap:%s "
        "client_assoc.chan_width_set:%u\n",
        (bss_->ht_cap != nullptr) ? "yes" : "no",
        (bss_->ht_cap == nullptr)
            ? "invalid"
            : (bss_->ht_cap->ht_cap_info.chan_width_set == to_enum_type(wlan_mlme::ChanWidthSet::TWENTY_ONLY))
                  ? "20"
                  : "40",
        client_assoc.has_ht_cap ? "yes" : "no",
        static_cast<uint8_t>(client_assoc.ht_cap.ht_cap_info.chan_width_set()));

    if (bss_->ht_cap == nullptr) {
        debugjoin("Disable CBW40: no HT support in target BSS\n");
        return false;
    }
    if (bss_->ht_cap->ht_cap_info.chan_width_set == to_enum_type(wlan_mlme::ChanWidthSet::TWENTY_ONLY)) {
        debugjoin("Disable CBW40: no CBW40 support in target BSS\n");
        return false;
    }

    if (!client_assoc.has_ht_cap) {
        debugjoin("Disable CBW40: no HT support in the this device\n");
        return false;
    }
    if (client_assoc.ht_cap.ht_cap_info.chan_width_set() == HtCapabilityInfo::TWENTY_ONLY) {
        debugjoin("Disable CBW40: no CBW40 support in the this device\n");
        return false;
    }

    return true;
}

bool Station::IsQosReady() const {
    // TODO(NET-567,NET-599): Determine for each outbound data frame,
    // given the result of the dynamic capability negotiation, data frame
    // classification, and QoS policy.

    // Aruba / Ubiquiti are confirmed to be compatible with QoS field for the BlockAck session,
    // independently of 40MHz operation.
    return assoc_ctx_.is_ht;
}

CapabilityInfo Station::OverrideCapability(CapabilityInfo cap) const {
    // parameter is of 2 bytes
    cap.set_ess(1);            // reserved in client role. 1 for better interop.
    cap.set_ibss(0);           // reserved in client role
    cap.set_cf_pollable(0);    // not supported
    cap.set_cf_poll_req(0);    // not supported
    cap.set_privacy(0);        // reserved in client role
    cap.set_spectrum_mgmt(0);  // not supported
    return cap;
}

zx_status_t Station::OverrideHtCapability(HtCapabilities* ht_cap) const {
    // TODO(porce): Determine which value to use for each field
    // (a) client radio capabilities, as reported by device driver
    // (b) intersection of (a) and radio configurations
    // (c) intersection of (b) and BSS capabilities
    // (d) intersection of (c) and radio configuration

    ZX_DEBUG_ASSERT(ht_cap != nullptr);
    if (ht_cap == nullptr) { return ZX_ERR_INVALID_ARGS; }

    HtCapabilityInfo& hci = ht_cap->ht_cap_info;
    if (!IsCbw40Rx()) { hci.set_chan_width_set(HtCapabilityInfo::TWENTY_ONLY); }

    // TODO(NET-1403): Lift up the restriction after broader interop and assoc_ctx_ adjustment.
    hci.set_tx_stbc(0);

    return ZX_OK;
}

uint8_t Station::GetTid() {
    // IEEE Std 802.11-2016, 3.1(Traffic Identifier), 5.1.1.1 (Data Service - General), 9.4.2.30
    // (Access Policy), 9.2.4.5.2 (TID subfield) Related topics: QoS facility, TSPEC, WM, QMF, TXOP.
    // A TID is from [0, 15], and is assigned to an MSDU in the layers above the MAC.
    // [0, 7] identify Traffic Categories (TCs)
    // [8, 15] identify parameterized Traffic Streams (TSs).

    // TODO(NET-599): Implement QoS policy engine.
    return 0;
}

uint8_t Station::GetTid(const EthFrame& frame) {
    return GetTid();
}

zx_status_t Station::SetAssocContext(const MgmtFrameView<AssociationResponse>& frame) {
    assoc_ctx_ = AssocContext{};
    assoc_ctx_.ts_start = timer_mgr_.Now();
    assoc_ctx_.bssid = common::MacAddr(bss_->bssid.data());
    assoc_ctx_.aid = frame.body()->aid & kAidMask;

    AssocContext ap{};
    ap.cap = frame.body()->cap;

    auto ie_chains = frame.body()->elements;
    size_t ie_chains_len = frame.body_len() - frame.body()->len();
    auto status = ParseAssocRespIe(ie_chains, ie_chains_len, &ap);
    if (status != ZX_OK) {
        debugf("failed to parse AssocResp. status %d\n", status);
        return status;
    }
    debugjoin("rxed AssocResp:[%s]\n", debug::Describe(ap).c_str());

    auto ifc_info = device_->GetWlanInfo().ifc_info;
    auto client = ToAssocContext(ifc_info, join_chan_);
    debugjoin("from WlanInfo: [%s]\n", debug::Describe(client).c_str());

    assoc_ctx_.cap = IntersectCapInfo(ap.cap, client.cap);
    SetAssocCtxSuppRates(ap, client, &assoc_ctx_.supported_rates, &assoc_ctx_.ext_supported_rates);

    assoc_ctx_.has_ht_cap = ap.has_ht_cap && client.has_ht_cap;
    if (assoc_ctx_.has_ht_cap) {
        // TODO(porce): Supported MCS Set field from the outcome of the intersection
        // requires the conditional treatment depending on the value of the following fields:
        // - "Tx MCS Set Defined"
        // - "Tx Rx MCS Set Not Equal"
        // - "Tx Maximum Number Spatial Streams Supported"
        // - "Tx Unequal Modulation Supported"
        assoc_ctx_.ht_cap = IntersectHtCap(ap.ht_cap, client.ht_cap);

        // Override the outcome of IntersectHtCap(), which is role agnostic.

        // If AP can't rx STBC, then the client shall not tx STBC.
        // Otherwise, the client shall do what it can do.
        if (ap.ht_cap.ht_cap_info.rx_stbc() == 0) {
            assoc_ctx_.ht_cap.ht_cap_info.set_tx_stbc(0);
        } else {
            assoc_ctx_.ht_cap.ht_cap_info.set_tx_stbc(client.ht_cap.ht_cap_info.tx_stbc());
        }

        // If AP can't tx STBC, then the client shall not expect to rx STBC.
        // Otherwise, the client shall do what it can do.
        if (ap.ht_cap.ht_cap_info.tx_stbc() == 0) {
            assoc_ctx_.ht_cap.ht_cap_info.set_rx_stbc(0);
        } else {
            assoc_ctx_.ht_cap.ht_cap_info.set_rx_stbc(client.ht_cap.ht_cap_info.rx_stbc());
        }

        assoc_ctx_.has_ht_op = ap.has_ht_op;
        if (assoc_ctx_.has_ht_op) { assoc_ctx_.ht_op = ap.ht_op; }
    }
    assoc_ctx_.has_vht_cap = ap.has_vht_cap && client.has_vht_cap;
    if (assoc_ctx_.has_vht_cap) {
        assoc_ctx_.vht_cap = IntersectVhtCap(ap.vht_cap, client.vht_cap);
        assoc_ctx_.has_vht_cap = ap.has_vht_op;
        if (assoc_ctx_.has_vht_op) { assoc_ctx_.vht_op = ap.vht_op; }
    }
    debugjoin("final AssocCtx:[%s]\n", debug::Describe(assoc_ctx_).c_str());

    assoc_ctx_.is_ht = assoc_ctx_.has_ht_cap;
    assoc_ctx_.is_cbw40_rx =
        assoc_ctx_.has_ht_cap &&
        ap.ht_cap.ht_cap_info.chan_width_set() == HtCapabilityInfo::TWENTY_FORTY &&
        client.ht_cap.ht_cap_info.chan_width_set() != HtCapabilityInfo::TWENTY_FORTY;

    // TODO(porce): Test capabilities and configurations of the client and its BSS.
    // TODO(porce): Ralink dependency on BlockAck, AMPDU handling
    assoc_ctx_.is_cbw40_tx = false;

    return ZX_OK;
}

zx_status_t Station::NotifyAssocContext() {
    wlan_assoc_ctx_t ddk{};
    assoc_ctx_.bssid.CopyTo(ddk.bssid);
    ddk.aid = assoc_ctx_.aid;

    auto& sr = assoc_ctx_.supported_rates;
    ZX_DEBUG_ASSERT(sr.size() <= WLAN_MAC_SUPPORTED_RATES_MAX_LEN);
    ddk.supported_rates_cnt = static_cast<uint8_t>(sr.size());
    std::copy(sr.begin(), sr.end(), ddk.supported_rates);

    auto& esr = assoc_ctx_.ext_supported_rates;
    ZX_DEBUG_ASSERT(esr.size() <= WLAN_MAC_EXT_SUPPORTED_RATES_MAX_LEN);
    ddk.ext_supported_rates_cnt = static_cast<uint8_t>(esr.size());
    std::copy(esr.begin(), esr.end(), ddk.ext_supported_rates);

    ddk.has_ht_cap = assoc_ctx_.has_ht_cap;
    if (ddk.has_ht_cap) { ddk.ht_cap = assoc_ctx_.ht_cap.ToDdk(); }

    ddk.has_ht_op = assoc_ctx_.has_ht_op;
    if (ddk.has_ht_op) { ddk.ht_op = assoc_ctx_.ht_op.ToDdk(); }

    ddk.has_vht_cap = assoc_ctx_.has_vht_cap;
    if (ddk.has_vht_cap) { ddk.vht_cap = assoc_ctx_.vht_cap.ToDdk(); }

    ddk.has_vht_op = assoc_ctx_.has_vht_op;
    if (ddk.has_vht_op) { ddk.vht_op = assoc_ctx_.vht_op.ToDdk(); }

    return device_->ConfigureAssoc(&ddk);
}

wlan_stats::ClientMlmeStats Station::stats() const {
    return stats_.ToFidl();
}

void Station::ResetStats() {
    stats_.Reset();
}

const wlan_band_info_t* FindBand(const wlan_info_t& ifc_info, bool is_5ghz) {
    ZX_DEBUG_ASSERT(ifc_info.num_bands <= WLAN_MAX_BANDS);

    for (uint8_t idx = 0; idx < ifc_info.num_bands; idx++) {
        auto bi = &ifc_info.bands[idx];
        auto base_freq = bi->supported_channels.base_freq;

        if (is_5ghz && base_freq == common::kBaseFreq5Ghz) {
            return bi;
        } else if (!is_5ghz && base_freq == common::kBaseFreq2Ghz) {
            return bi;
        }
    }

    return nullptr;
}

// TODO(NET-1287): Refactor together with Bss::ParseIE()
zx_status_t ParseAssocRespIe(const uint8_t* ie_chains, size_t ie_chains_len,
                             AssocContext* assoc_ctx) {
    ZX_DEBUG_ASSERT(assoc_ctx != nullptr);

    ElementReader reader(ie_chains, ie_chains_len);
    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) { break; }

        switch (hdr->id) {
        case element_id::kSuppRates: {
            auto ie = reader.read<SupportedRatesElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            for (uint8_t i = 0; i < ie->hdr.len; i++) {
                assoc_ctx->supported_rates.push_back(ie->rates[i]);
            }
            break;
        }
        case element_id::kExtSuppRates: {
            auto ie = reader.read<ExtendedSupportedRatesElement>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            for (uint8_t i = 0; i < ie->hdr.len; i++) {
                assoc_ctx->ext_supported_rates.push_back(ie->rates[i]);
            }
            break;
        }
        case element_id::kHtCapabilities: {
            auto ie = reader.read<HtCapabilities>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->has_ht_cap = true;
            assoc_ctx->ht_cap = *ie;
            break;
        }
        case element_id::kHtOperation: {
            auto ie = reader.read<HtOperation>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->has_ht_op = true;
            assoc_ctx->ht_op = *ie;
            break;
        }
        case element_id::kVhtCapabilities: {
            auto ie = reader.read<VhtCapabilities>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->has_vht_cap = true;
            assoc_ctx->vht_cap = *ie;
            break;
        }
        case element_id::kVhtOperation: {
            auto ie = reader.read<VhtOperation>();
            if (ie == nullptr) { return ZX_ERR_INTERNAL; }
            assoc_ctx->has_vht_op = true;
            assoc_ctx->vht_op = *ie;
            break;
        }
        default:
            reader.skip(sizeof(ElementHeader) + hdr->len);
            break;
        }
    }

    return ZX_OK;
}

AssocContext ToAssocContext(const wlan_info_t& ifc_info, const wlan_channel_t join_chan) {
    AssocContext assoc_ctx{};

    assoc_ctx.cap = CapabilityInfo::FromDdk(ifc_info.caps);

    auto band_info = FindBand(ifc_info, common::Is5Ghz(join_chan));

    for (uint8_t rate : band_info->basic_rates) {
        if (rate == 0) { break; }  // basic_rates has fixed-length and is "null-terminated".
        // SupportedRates Element can hold only 8 rates.
        if (assoc_ctx.supported_rates.size() < SupportedRatesElement::kMaxLen) {
            assoc_ctx.supported_rates.emplace_back(rate);
        } else {
            assoc_ctx.ext_supported_rates.emplace_back(rate);
        }
    }

    if (ifc_info.supported_phys & WLAN_PHY_HT) {
        assoc_ctx.has_ht_cap = true;
        static_assert(sizeof(HtCapabilities) == sizeof(wlan_ht_caps_t) + sizeof(ElementHeader),
                      "HtCap size mimatch between IE and DDK");
        auto elem = reinterpret_cast<uint8_t*>(&assoc_ctx.ht_cap);
        memcpy(elem + sizeof(ElementHeader), &band_info->ht_caps, sizeof(wlan_ht_caps_t));
    }

    if (band_info->vht_supported) {
        assoc_ctx.has_vht_cap = true;
        static_assert(sizeof(VhtCapabilities) == sizeof(wlan_vht_caps_t) + sizeof(ElementHeader),
                      "VhtCap size mimatch between IE and DDK");
        auto elem = reinterpret_cast<uint8_t*>(&assoc_ctx.vht_cap);
        memcpy(elem + sizeof(ElementHeader), &band_info->vht_caps, sizeof(wlan_vht_caps_t));
    }

    return assoc_ctx;
}

void SetAssocCtxSuppRates(const AssocContext& ap, const AssocContext& client,
                          std::vector<SupportedRate>* supp_rates,
                          std::vector<SupportedRate>* ext_rates) {
    auto ap_rates(ap.supported_rates);
    ap_rates.insert(ap_rates.end(), ap.ext_supported_rates.cbegin(), ap.ext_supported_rates.cend());
    auto client_rates(client.supported_rates);
    client_rates.insert(client_rates.end(), client.ext_supported_rates.cbegin(),
                        client.ext_supported_rates.cend());

    *supp_rates = IntersectRatesAp(ap_rates, client_rates);

    // SupportedRates Element can hold at most 8 rates. The rest go to ExtSupportedRates
    if (supp_rates->size() > SupportedRatesElement::kMaxLen) {
        std::move(supp_rates->cbegin() + SupportedRatesElement::kMaxLen, supp_rates->cend(),
                  std::back_inserter(*ext_rates));
        supp_rates->resize(SupportedRatesElement::kMaxLen);
    }
}

std::string Station::GetPhyStr() const {
    if (assoc_ctx_.is_vht) {
        return "802.11ac VHT";
    } else if (assoc_ctx_.is_ht) {
        return "802.11n HT";
    } else if (common::Is5Ghz(join_chan_)) {
        return "802.11a";
    } else {
        return "802.11g";
    }
}

}  // namespace wlan
