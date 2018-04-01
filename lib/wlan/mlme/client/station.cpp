// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/station.h>

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/sequence.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <cstring>
#include <utility>

namespace wlan {

// TODO(hahnr): Revisit frame construction to reduce boilerplate code.

static constexpr zx_duration_t kAssocTimeoutTu = ZX_NSEC(20);
static constexpr zx_duration_t kSignalReportTimeoutTu = ZX_NSEC(10);

Station::Station(DeviceInterface* device, fbl::unique_ptr<Timer> timer)
    : device_(device), timer_(std::move(timer)) {
    (void)assoc_timeout_;
    bssid_.Reset();
}

void Station::Reset() {
    debugfn();

    timer_->CancelTimer();
    state_ = WlanState::kUnjoined;
    bss_.reset();
    join_timeout_ = zx::time();
    auth_timeout_ = zx::time();
    last_seen_ = zx::time();
    bssid_.Reset();
}

zx_status_t Station::HandleMlmeMessage(const wlan_mlme::Method& method) {
    // Always allow MLME-JOIN.request.
    if (method == wlan_mlme::Method::JOIN_request) { return ZX_OK; }
    // Drop other MLME requests if there is no BSSID set yet.
    return (bssid() == nullptr ? ZX_ERR_STOP : ZX_OK);
}

zx_status_t Station::HandleMlmeJoinReq(const wlan_mlme::JoinRequest& req) {
    debugfn();

    if (state_ != WlanState::kUnjoined) {
        warnf("already joined; resetting station\n");
        Reset();
    }

    // Clone request to take ownership of the BSS.
    bss_ = wlan_mlme::BSSDescription::New();
    req.selected_bss.Clone(bss_.get());
    bssid_.Set(bss_->bssid.data());

    auto chan = GetBssChan();

    // TODO(NET-449): Move this logic to policy engine
    // Validation and sanitization
    if (!common::IsValidChan(chan)) {
        wlan_channel_t chan_sanitized = chan;
        chan_sanitized.cbw = common::GetValidCbw(chan);
        errorf("Wlanstack attempts to configure an invalid channel: %s. Falling back to %s\n",
               common::ChanStr(chan).c_str(), common::ChanStr(chan_sanitized).c_str());
        chan = chan_sanitized;
    }
    if (IsCbw40RxReady()) {
        // Override with CBW40 support
        wlan_channel_t chan_override = chan;
        chan_override.cbw = CBW40;
        chan_override.cbw = common::GetValidCbw(chan_override);

        infof("CBW40 Rx is ready. Overriding the channel configuration from %s to %s\n",
              common::ChanStr(chan).c_str(), common::ChanStr(chan_override).c_str());
        chan = chan_override;
    }

    debugjoin("setting channel to %s\n", common::ChanStr(chan).c_str());
    zx_status_t status = device_->SetChannel(chan);

    if (status != ZX_OK) {
        errorf("could not set wlan channel to %s (status %d)\n", common::ChanStr(chan).c_str(),
               status);
        Reset();
        service::SendJoinResponse(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    join_chan_ = chan;
    join_timeout_ = deadline_after_bcn_period(req.join_failure_timeout);
    status = timer_->SetTimer(join_timeout_);
    if (status != ZX_OK) {
        errorf("could not set join timer: %d\n", status);
        Reset();
        service::SendJoinResponse(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
    }

    // TODO(hahnr): Update when other BSS types are supported.
    wlan_bss_config_t cfg{
        .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
        .remote = true,
    };
    bssid_.CopyTo(cfg.bssid);
    device_->ConfigureBss(&cfg);
    return status;
}  // namespace wlan

zx_status_t Station::HandleMlmeAuthReq(const wlan_mlme::AuthenticateRequest& req) {
    debugfn();

    if (bss_ == nullptr) { return ZX_ERR_BAD_STATE; }

    // TODO(tkilbourn): better result codes
    common::MacAddr peer_sta_addr(req.peer_sta_address.data());
    if (bssid_ != peer_sta_addr) {
        errorf("cannot authenticate before joining\n");
        return service::SendAuthResponse(device_, bssid_,
                                         wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kUnjoined) {
        errorf("must join before authenticating\n");
        return service::SendAuthResponse(device_, bssid_,
                                         wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
    if (state_ != WlanState::kUnauthenticated) {
        warnf("already authenticated; sending request anyway\n");
    }
    if (req.auth_type != wlan_mlme::AuthenticationTypes::OPEN_SYSTEM) {
        // TODO(tkilbourn): support other authentication types
        // TODO(tkilbourn): set the auth_alg_ when we support other authentication types
        errorf("only OpenSystem authentication is supported\n");
        return service::SendAuthResponse(device_, bssid_,
                                         wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    debugjoin("authenticating to %s\n", MACSTR(bssid_));

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Authentication>(&packet, 0);
    if (packet == nullptr) {
        errorf("authing: failed to build a frame\n");
        return ZX_ERR_NO_RESOURCES;
    }

    auto hdr = frame.hdr;
    const common::MacAddr& mymac = device_->GetState()->address();
    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    FillTxInfo(&packet, *hdr);

    // TODO(tkilbourn): this assumes Open System authentication
    auto auth = frame.body;
    auth->auth_algorithm_number = auth_alg_;
    auth->auth_txn_seq_number = 1;
    auth->status_code = 0;  // Reserved, so set to 0

    finspect("Outbound Mgmt Frame(Auth): %s\n", debug::Describe(*hdr).c_str());
    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send auth packet: %d\n", status);
        service::SendAuthResponse(device_, bssid_, wlan_mlme::AuthenticateResultCodes::REFUSED);
        return status;
    }

    auth_timeout_ = deadline_after_bcn_period(req.auth_failure_timeout);
    status = timer_->SetTimer(auth_timeout_);
    if (status != ZX_OK) {
        errorf("could not set auth timer: %d\n", status);
        // This is the wrong result code, but we need to define our own codes at some later time.
        service::SendAuthResponse(device_, bssid_,
                                  wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
        // TODO(tkilbourn): reset the station?
    }
    return status;
}

zx_status_t Station::HandleMlmeDeauthReq(const wlan_mlme::DeauthenticateRequest& req) {
    debugfn();

    if (state_ != WlanState::kAssociated && state_ != WlanState::kAuthenticated) {
        errorf("not associated or authenticated; ignoring deauthenticate request\n");
        return ZX_OK;
    }

    if (bss_ == nullptr) { return ZX_ERR_BAD_STATE; }

    // Check whether the request wants to deauthenticate from this STA's BSS.
    common::MacAddr peer_sta_addr(req.peer_sta_address.data());
    if (bssid_ != peer_sta_addr) { return ZX_OK; }

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Deauthentication>(&packet, 0);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const common::MacAddr& mymac = device_->GetState()->address();
    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    FillTxInfo(&packet, *hdr);

    auto deauth = frame.body;
    deauth->reason_code = req.reason_code;

    finspect("Outbound Mgmt Frame(Deauth): %s\n", debug::Describe(*hdr).c_str());
    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send deauth packet: %d\n", status);
        // Deauthenticate nevertheless. IEEE isn't clear on what we are supposed to do.
    }

    infof("deauthenticating from %s, reason=%u\n", bss_->ssid->data(), req.reason_code);

    // TODO(hahnr): Refactor once we have the new state machine.
    state_ = WlanState::kUnauthenticated;
    device_->SetStatus(0);
    controlled_port_ = PortState::kBlocked;
    service::SendDeauthResponse(device_, bssid_);

    return ZX_OK;
}

zx_status_t Station::HandleMlmeAssocReq(const wlan_mlme::AssociateRequest& req) {
    debugfn();

    if (bss_ == nullptr) { return ZX_ERR_BAD_STATE; }

    // TODO(tkilbourn): better result codes
    common::MacAddr peer_sta_addr(req.peer_sta_address.data());
    if (bssid_ != peer_sta_addr) {
        errorf("bad peer STA address for association\n");
        return service::SendAuthResponse(device_, bssid_,
                                         wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kUnjoined || state_ == WlanState::kUnauthenticated) {
        errorf("must authenticate before associating\n");
        return service::SendAuthResponse(device_, bssid_,
                                         wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kAssociated) {
        warnf("already authenticated; sending request anyway\n");
    }

    debugjoin("associating to %s\n", MACSTR(bssid_));

    size_t body_payload_len = 128;
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AssociationRequest>(&packet, body_payload_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    // TODO(tkilbourn): a lot of this is hardcoded for now. Use device capabilities to set up the
    // request.
    auto hdr = frame.hdr;
    const common::MacAddr& mymac = device_->GetState()->address();
    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    FillTxInfo(&packet, *hdr);

    auto assoc = frame.body;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(0);  // For robustness. TODO(porce): Enforce Ralink
    assoc->listen_interval = 0;

    ElementWriter w(assoc->elements,
                    packet->len() - sizeof(MgmtFrameHeader) - sizeof(AssociationRequest));
    if (!w.write<SsidElement>(bss_->ssid->data())) {
        errorf("could not write ssid \"%s\" to association request\n", bss_->ssid->data());
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }
    // TODO(tkilbourn): determine these rates based on hardware and the AP
    std::vector<uint8_t> rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};

    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("could not write supported rates\n");
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    // Write RSNE from MLME-Association.request if available.
    if (req.rsn) {
        if (!w.write<RsnElement>(req.rsn->data(), req.rsn->size())) { return ZX_ERR_IO; }
    }

    if (IsHTReady()) {
        HtCapabilities htc = BuildHtCapabilities();
        if (!w.write<HtCapabilities>(htc.ht_cap_info, htc.ampdu_params, htc.mcs_set, htc.ht_ext_cap,
                                     htc.txbf_cap, htc.asel_cap)) {
            errorf("could not write HtCapabilities\n");
            service::SendAssocResponse(device_,
                                       wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
            return ZX_ERR_IO;
        }
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(assoc->Validate(w.size()));

    body_payload_len = w.size();
    size_t frame_len = hdr->len() + sizeof(AssociationRequest) + body_payload_len;

    zx_status_t status = packet->set_len(frame_len);
    if (status != ZX_OK) {
        errorf("could not set packet length to %zu: %d\n", frame_len, status);
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    finspect("Outbound Mgmt Frame (AssocReq): %s\n", debug::Describe(*hdr).c_str());
    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send assoc packet: %d\n", status);
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    // TODO(tkilbourn): get the assoc timeout from somewhere
    assoc_timeout_ = deadline_after_bcn_period(kAssocTimeoutTu);
    status = timer_->SetTimer(assoc_timeout_);
    if (status != ZX_OK) {
        errorf("could not set auth timer: %d\n", status);
        // This is the wrong result code, but we need to define our own codes at some later time.
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        // TODO(tkilbourn): reset the station?
    }
    return status;
}

zx_status_t Station::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    // Drop management frames if either, there is no BSSID set yet,
    // or the frame is not from the BSS.
    if (bssid() == nullptr || *bssid() != hdr.addr3) { return ZX_ERR_STOP; }
    return ZX_OK;
}

// TODO(hahnr): Support ProbeResponses.
zx_status_t Station::HandleBeacon(const ImmutableMgmtFrame<Beacon>& frame,
                                  const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    ZX_DEBUG_ASSERT(frame.hdr->fc.subtype() == ManagementSubtype::kBeacon);
    ZX_DEBUG_ASSERT(frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));

    avg_rssi_.add(rxinfo.rssi);

    // TODO(tkilbourn): update any other info (like rolling average of rssi)
    last_seen_ = timer_->Now();
    if (join_timeout_ > zx::time()) {
        join_timeout_ = zx::time();
        timer_->CancelTimer();
        state_ = WlanState::kUnauthenticated;
        debugjoin("joined %s\n", bss_->ssid->data());
        return service::SendJoinResponse(device_, wlan_mlme::JoinResultCodes::SUCCESS);
    }

    auto bcn = frame.body;
    size_t elt_len = frame.body_len - sizeof(Beacon);
    ElementReader reader(bcn->elements, elt_len);
    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) break;

        switch (hdr->id) {
        case element_id::kTim: {
            auto tim = reader.read<TimElement>();
            if (tim == nullptr) goto done_iter;
            if (tim->traffic_buffered(aid_)) { SendPsPoll(); }
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

zx_status_t Station::HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                          const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->fc.subtype() == ManagementSubtype::kAuthentication);
    ZX_DEBUG_ASSERT(frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));

    if (state_ != WlanState::kUnauthenticated) {
        // TODO(tkilbourn): should we process this Authentication packet anyway? The spec is
        // unclear.
        debugjoin("unexpected authentication frame\n");
        return ZX_OK;
    }

    auto auth = frame.body;
    if (auth->auth_algorithm_number != auth_alg_) {
        errorf("mismatched authentication algorithm (expected %u, got %u)\n", auth_alg_,
               auth->auth_algorithm_number);
        return ZX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): this only makes sense for Open System.
    if (auth->auth_txn_seq_number != 2) {
        errorf("unexpected auth txn sequence number (expected 2, got %u)\n",
               auth->auth_txn_seq_number);
        return ZX_ERR_BAD_STATE;
    }

    if (auth->status_code != status_code::kSuccess) {
        errorf("authentication failed (status code=%u)\n", auth->status_code);
        // TODO(tkilbourn): is this the right result code?
        service::SendAuthResponse(device_, bssid_,
                                  wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_BAD_STATE;
    }

    common::MacAddr bssid(bss_->bssid.data());
    debugjoin("authenticated to %s\n", MACSTR(bssid));
    state_ = WlanState::kAuthenticated;
    auth_timeout_ = zx::time();
    timer_->CancelTimer();
    service::SendAuthResponse(device_, bssid_, wlan_mlme::AuthenticateResultCodes::SUCCESS);
    return ZX_OK;
}

zx_status_t Station::HandleDeauthentication(const ImmutableMgmtFrame<Deauthentication>& frame,
                                            const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->fc.subtype() == ManagementSubtype::kDeauthentication);
    ZX_DEBUG_ASSERT(frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));

    if (state_ != WlanState::kAssociated && state_ != WlanState::kAuthenticated) {
        debugjoin("got spurious deauthenticate; ignoring\n");
        return ZX_OK;
    }

    auto deauth = frame.body;
    infof("deauthenticating from %s, reason=%u\n", bss_->ssid->data(), deauth->reason_code);

    state_ = WlanState::kUnauthenticated;
    device_->SetStatus(0);
    controlled_port_ = PortState::kBlocked;

    return service::SendDeauthIndication(device_, bssid_, deauth->reason_code);
}

zx_status_t Station::HandleAssociationResponse(const ImmutableMgmtFrame<AssociationResponse>& frame,
                                               const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->fc.subtype() == ManagementSubtype::kAssociationResponse);
    ZX_DEBUG_ASSERT(frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));

    if (state_ != WlanState::kAuthenticated) {
        // TODO(tkilbourn): should we process this Association response packet anyway? The spec is
        // unclear.
        debugjoin("unexpected association response frame\n");
        return ZX_OK;
    }

    auto assoc = frame.body;
    if (assoc->status_code != status_code::kSuccess) {
        errorf("association failed (status code=%u)\n", assoc->status_code);
        // TODO(tkilbourn): map to the correct result code
        service::SendAssocResponse(device_,
                                   wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_BAD_STATE;
    }

    common::MacAddr bssid(bss_->bssid.data());
    state_ = WlanState::kAssociated;
    assoc_timeout_ = zx::time();
    aid_ = assoc->aid & kAidMask;
    timer_->CancelTimer();
    service::SendAssocResponse(device_, wlan_mlme::AssociateResultCodes::SUCCESS, aid_);

    signal_report_timeout_ = deadline_after_bcn_period(kSignalReportTimeoutTu);
    timer_->SetTimer(signal_report_timeout_);
    avg_rssi_.reset();
    avg_rssi_.add(rxinfo.rssi);
    service::SendSignalReportIndication(device_, rxinfo.rssi);

    // Open port if user connected to an open network.
    if (bss_->rsn.is_null()) {
        debugjoin("802.1X controlled port is now open\n");
        controlled_port_ = PortState::kOpen;
        device_->SetStatus(ETH_STATUS_ONLINE);
    }

    const common::MacAddr& mymac = device_->GetState()->address();
    infof("NIC %s associated with \"%s\"(%s) in channel %s, %s, %s\n", mymac.ToString().c_str(),
          bss_->ssid->data(), bssid.ToString().c_str(), common::ChanStr(GetJoinChan()).c_str(),
          common::BandStr(GetJoinChan()).c_str(), IsHTReady() ? "802.11n HT" : "802.11g/a");

    // TODO(porce): Time when to establish BlockAck session
    // Handle MLME-level retry, if MAC-level retry ultimately fails
    // Wrap this as EstablishBlockAckSession(peer_mac_addr)
    // Signal to lower MAC for proper session handling
    SendAddBaRequestFrame();
    return ZX_OK;
}

zx_status_t Station::HandleDisassociation(const ImmutableMgmtFrame<Disassociation>& frame,
                                          const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->fc.subtype() == ManagementSubtype::kDisassociation);
    ZX_DEBUG_ASSERT(frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));

    if (state_ != WlanState::kAssociated) {
        debugjoin("got spurious disassociate; ignoring\n");
        return ZX_OK;
    }

    auto disassoc = frame.body;
    common::MacAddr bssid(bss_->bssid.data());
    infof("disassociating from %s(%s), reason=%u\n", MACSTR(bssid), bss_->ssid->data(),
          disassoc->reason_code);

    state_ = WlanState::kAuthenticated;
    device_->SetStatus(0);
    controlled_port_ = PortState::kBlocked;

    signal_report_timeout_ = zx::time();
    timer_->CancelTimer();

    return service::SendDisassociateIndication(device_, bssid, disassoc->reason_code);
}

zx_status_t Station::HandleAddBaRequestFrame(const ImmutableMgmtFrame<AddBaRequestFrame>& rx_frame,
                                             const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(rx_frame.hdr->fc.subtype() == ManagementSubtype::kAction);
    ZX_DEBUG_ASSERT(rx_frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));
    ZX_DEBUG_ASSERT(rx_frame.body->category == action::Category::kBlockAck);
    ZX_DEBUG_ASSERT(rx_frame.body->action == action::BaAction::kAddBaRequest);

    auto addbar = rx_frame.body;
    finspect("Inbound ADDBA Req frame: len %zu\n", rx_frame.body_len);
    finspect("  addba req: %s\n", debug::Describe(*addbar).c_str());

    // Construct AddBaResponse frame
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AddBaResponseFrame>(&packet, 0);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const common::MacAddr& mymac = device_->GetState()->address();
    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    FillTxInfo(&packet, *hdr);

    auto resp = frame.body;
    resp->category = action::Category::kBlockAck;
    resp->action = action::BaAction::kAddBaResponse;
    resp->dialog_token = addbar->dialog_token;

    // TODO(porce): Implement DelBa as a response to AddBar for decline

    // Note: Returning AddBaResponse with status_code::kRefused seems ineffective.
    // ArubaAP is persistent not honoring that.
    resp->status_code = status_code::kSuccess;

    // TODO(porce): Query the radio chipset capability to build the response.
    // TODO(NET-567): Use the outcome of the association negotiation
    resp->params.set_amsdu(0);  // For Aruba
    resp->params.set_policy(BlockAckParameters::kImmediate);
    resp->params.set_tid(addbar->params.tid());

    // TODO(porce): Once chipset capability is ready, refactor below buffer_size
    // calculation.
    auto buffer_size_ap = addbar->params.buffer_size();
    constexpr size_t buffer_size_ralink = 64;
    auto buffer_size = (buffer_size_ap <= buffer_size_ralink) ? buffer_size_ap : buffer_size_ralink;
    resp->params.set_buffer_size(buffer_size);

    resp->timeout = addbar->timeout;

    finspect("Outbound ADDBA Resp frame: len %zu\n", packet->len());
    finspect("Outbound Mgmt Frame(ADDBA Resp): %s\n", debug::Describe(*hdr).c_str());

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send AddBaResponse: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t Station::HandleAddBaResponseFrame(
    const ImmutableMgmtFrame<AddBaResponseFrame>& rx_frame, const wlan_rx_info& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(rx_frame.hdr != nullptr);
    ZX_DEBUG_ASSERT(rx_frame.body != nullptr);
    ZX_DEBUG_ASSERT(rx_frame.hdr->fc.subtype() == ManagementSubtype::kAction);
    ZX_DEBUG_ASSERT(rx_frame.hdr->addr3 == common::MacAddr(bss_->bssid.data()));
    ZX_DEBUG_ASSERT(rx_frame.body->category == action::Category::kBlockAck);
    ZX_DEBUG_ASSERT(rx_frame.body->action == action::BaAction::kAddBaResponse);

    auto hdr = rx_frame.hdr;
    auto addba_resp = rx_frame.body;
    finspect("Inbound ADDBA Resp frame: len %zu\n", hdr->len() + rx_frame.body_len);
    finspect("  addba resp: %s\n", debug::Describe(*addba_resp).c_str());

    // TODO(porce): Keep the result of negotiation.
    return ZX_OK;
}

zx_status_t Station::HandleDataFrame(const DataFrameHeader& hdr) {
    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    auto from_bss = (bssid() != nullptr && *bssid() == hdr.addr2);
    if (!from_bss) { return ZX_ERR_STOP; }
    return ZX_OK;
}

zx_status_t Station::HandleNullDataFrame(const ImmutableDataFrame<NilHeader>& frame,
                                         const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->fc.subtype() == DataSubtype::kNull);
    ZX_DEBUG_ASSERT(bssid() != nullptr);
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == common::MacAddr(bss_->bssid.data()));
    ZX_DEBUG_ASSERT(state_ == WlanState::kAssociated);

    // Take signal strength into account.
    avg_rssi_.add(rxinfo.rssi);

    // Some AP's such as Netgear Routers send periodic NULL data frames to test whether a client
    // timed out. The client must respond with a NULL data frame itself to not get
    // deauthenticated.
    SendKeepAliveResponse();
    return ZX_OK;
}

zx_status_t Station::HandleDataFrame(const ImmutableDataFrame<LlcHeader>& frame,
                                     const wlan_rx_info_t& rxinfo) {
    debugfn();
    if (kFinspectEnabled) { DumpDataFrame(frame); }

    auto associated = (state_ == WlanState::kAssociated);
    if (!associated) {
        debugf("dropping data packet while not associated\n");
        return ZX_ERR_STOP;
    }

    ZX_DEBUG_ASSERT(bssid() != nullptr);
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == common::MacAddr(bss_->bssid.data()));
    ZX_DEBUG_ASSERT(state_ == WlanState::kAssociated);

    switch (frame.hdr->fc.subtype()) {
    case DataSubtype::kDataSubtype:
        // Fall-through
    case DataSubtype::kQosdata:  // For data frames within BlockAck session.
        break;
    default:
        warnf("unsupported data subtype %02x\n", frame.hdr->fc.subtype());
        return ZX_OK;
    }

    // Take signal strength into account.
    avg_rssi_.add(rxinfo.rssi);

    auto hdr = frame.hdr;
    auto llc = frame.body;

    // Forward EAPOL frames to SME.
    if (be16toh(llc->protocol_id) == kEapolProtocolId) {
        if (frame.body_len < sizeof(EapolFrame)) {
            warnf("short EAPOL frame; len = %zu", frame.body_len);
            return ZX_OK;
        }
        auto eapol = reinterpret_cast<const EapolFrame*>(llc->payload);
        uint16_t actual_body_len = frame.body_len;
        uint16_t expected_body_len = be16toh(eapol->packet_body_length);
        if (actual_body_len >= expected_body_len) {
            return service::SendEapolIndication(device_, *eapol, hdr->addr3, hdr->addr1);
        }
        return ZX_OK;
    }

    // Drop packets if RSNA was not yet established.
    if (controlled_port_ == PortState::kBlocked) { return ZX_OK; }

    // PS-POLL if there are more buffered unicast frames.
    if (hdr->fc.more_data() && hdr->addr1.IsUcast()) { SendPsPoll(); }

    const size_t eth_len = frame.body_len + sizeof(EthernetII);
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto eth_packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), eth_len));
    // no need to clear the packet since every byte is overwritten
    eth_packet->set_peer(Packet::Peer::kEthernet);
    auto eth = eth_packet->mut_field<EthernetII>(0);

    eth->dest = hdr->addr1;
    eth->src = hdr->addr3;

    eth->ether_type = llc->protocol_id;
    std::memcpy(eth->payload, llc->payload, frame.body_len - sizeof(LlcHeader));

    zx_status_t status = device_->SendEthernet(std::move(eth_packet));
    if (status != ZX_OK) { errorf("could not send ethernet data: %d\n", status); }
    return status;
}

zx_status_t Station::HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) {
    debugfn();

    // Drop Ethernet frames when not associated.
    auto bss_setup = (bssid() != nullptr);
    auto associated = (state_ == WlanState::kAssociated);
    if (!associated) { debugf("dropping eth packet while not associated\n"); }
    if (!bss_setup || !associated) { return ZX_OK; }

    auto eth = frame.hdr;
    const size_t buf_len = kDataFrameHdrLenMax + sizeof(LlcHeader) + frame.body_len;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    wlan_tx_info_t txinfo = {
        // TODO(porce): Determine PHY and CBW based on the association negotiation.
        .tx_flags = 0x0,
        .valid_fields =
            WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH | WLAN_TX_INFO_VALID_MCS,
        .phy = WLAN_PHY_HT,
        .cbw = CBW20,
        //.date_rate = 0x0,
        .mcs = 0x7,
    };

    auto wlan_packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    // no need to clear the whole packet; we memset the headers instead and copy over all bytes in
    // the payload
    wlan_packet->set_peer(Packet::Peer::kWlan);
    auto hdr = wlan_packet->mut_field<DataFrameHeader>(0);

    bool has_qos_ctrl = IsQosReady();
    bool has_ht_ctrl = false;
    // Set header
    std::memset(hdr, 0, kDataFrameHdrLenMax);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_subtype(has_qos_ctrl ? DataSubtype::kQosdata : DataSubtype::kDataSubtype);
    hdr->fc.set_to_ds(1);
    hdr->fc.set_from_ds(0);
    hdr->fc.set_htc_order(has_ht_ctrl ? 1 : 0);

    // Ensure all outgoing data frames are protected when RSNA is established.
    if (!bss_->rsn.is_null() && controlled_port_ == PortState::kOpen) {
        hdr->fc.set_protected_frame(1);
        txinfo.tx_flags |= WLAN_TX_INFO_FLAGS_PROTECTED;
    }

    hdr->addr1 = common::MacAddr(bss_->bssid.data());
    hdr->addr2 = eth->src;
    hdr->addr3 = eth->dest;

    // TODO(porce): Construct addr4 field

    if (IsCbw40TxReady()) {
        // Ralink appears to setup BlockAck session AND AMPDU handling
        // TODO(porce): Use a separate sequence number space in that case
        if (hdr->addr3.IsUcast()) {
            // 40MHz direction does not matter here.
            // Radio uses the operational channel setting. This indicates the bandwidth without
            // direction.
            txinfo.cbw = CBW40;
        }
    }

    if (hdr->HasQosCtrl()) {  // QoS Control field
        auto qos_ctrl = hdr->qos_ctrl();
        qos_ctrl->set_tid(GetTid(frame));
        qos_ctrl->set_eosp(0);
        qos_ctrl->set_ack_policy(ack_policy::kNormalAck);
        qos_ctrl->set_amsdu_present(0);
        qos_ctrl->set_byte(0);
    }

    // TODO(porce): Construct htc_order field

    SetSeqNo(hdr, &seq_);

    auto llc = wlan_packet->mut_field<LlcHeader>(hdr->len());
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = eth->ether_type;

    std::memcpy(llc->payload, eth->payload, frame.body_len);

    auto frame_len = hdr->len() + sizeof(LlcHeader) + frame.body_len;
    auto status = wlan_packet->set_len(frame_len);
    if (status != ZX_OK) {
        errorf("could not set data frame length to %zu: %d\n", frame_len, status);
        return status;
    }

    finspect("Outbound data frame: len %zu, hdr_len:%u body_len:%zu frame_len:%zu\n",
             wlan_packet->len(), hdr->len(), frame.body_len, frame_len);
    finspect("  wlan hdr: %s\n", debug::Describe(*hdr).c_str());
    finspect("  llc  hdr: %s\n", debug::Describe(*llc).c_str());
    finspect("  frame   : %s\n", debug::HexDump(wlan_packet->data(), frame_len).c_str());

    wlan_packet->CopyCtrlFrom(txinfo);

    status = device_->SendWlan(std::move(wlan_packet));
    if (status != ZX_OK) { errorf("could not send wlan data: %d\n", status); }
    return status;
}

zx_status_t Station::HandleTimeout() {
    debugfn();
    zx::time now = timer_->Now();
    if (join_timeout_ > zx::time() && now > join_timeout_) {
        debugjoin("join timed out; resetting\n");

        Reset();
        return service::SendJoinResponse(device_, wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
    }

    if (auth_timeout_ > zx::time() && now >= auth_timeout_) {
        debugjoin("auth timed out; moving back to joining\n");
        auth_timeout_ = zx::time();
        return service::SendAuthResponse(device_, bssid_,
                                         wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
    }

    if (assoc_timeout_ > zx::time() && now >= assoc_timeout_) {
        debugjoin("assoc timed out; moving back to authenticated\n");
        assoc_timeout_ = zx::time();
        // TODO(tkilbourn): need a better error code for this
        return service::SendAssocResponse(device_,
                                          wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY);
    }

    if (signal_report_timeout_ > zx::time() && now > signal_report_timeout_ &&
        state_ == WlanState::kAssociated) {
        signal_report_timeout_ = deadline_after_bcn_period(kSignalReportTimeoutTu);
        timer_->SetTimer(signal_report_timeout_);
        service::SendSignalReportIndication(device_, avg_rssi_.avg());
    }

    return ZX_OK;
}

zx_status_t Station::SendKeepAliveResponse() {
    if (state_ != WlanState::kAssociated) {
        warnf("cannot send keep alive response before being associated\n");
        return ZX_OK;
    }

    const common::MacAddr& mymac = device_->GetState()->address();
    size_t len = sizeof(DataFrameHeader);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_subtype(DataSubtype::kNull);
    hdr->fc.set_to_ds(1);

    common::MacAddr bssid(bss_->bssid.data());
    hdr->addr1 = bssid;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid;
    SetSeqNo(hdr, &seq_);

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send keep alive packet: %d\n", status);
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

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AddBaRequestFrame>(&packet, 0);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    auto req = frame.body;
    const common::MacAddr& mymac = device_->GetState()->address();
    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;
    SetSeqNo(hdr, &seq_);
    FillTxInfo(&packet, *hdr);

    req->category = action::Category::kBlockAck;
    req->action = action::BaAction::kAddBaRequest;
    // It appears there is no particular rule to choose the value for
    // dialog_token. See IEEE Std 802.11-2016, 9.6.5.2.
    req->dialog_token = 0x01;
    req->params.set_amsdu(0);  // Aruba
    req->params.set_policy(BlockAckParameters::BlockAckPolicy::kImmediate);
    req->params.set_tid(GetTid());  // TODO(porce): Communicate this with lower MAC.
    // TODO(porce): Fix the discrepancy of this value from the Ralink's TXWI ba_win_size setting
    req->params.set_buffer_size(64);
    req->timeout = 0;               // Disables the timeout
    req->seq_ctrl.set_fragment(0);  // TODO(porce): Send this down to the lower MAC
    req->seq_ctrl.set_starting_seq(1);

    finspect("Outbound ADDBA Req frame: len %zu\n", packet->len());
    finspect("  addba req: %s\n", debug::Describe(*req).c_str());
    finspect("Outbound Mgmt Frame(ADDBA Req): %s\n", debug::Describe(*hdr).c_str());

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send AddBaRequest: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t Station::HandleMlmeEapolReq(const wlan_mlme::EapolRequest& req) {
    debugfn();

    if (!bss_) { return ZX_ERR_BAD_STATE; }
    if (state_ != WlanState::kAssociated) {
        debugf("dropping MLME-EAPOL.request while not being associated. STA in state %d\n", state_);
        return ZX_OK;
    }

    size_t len = sizeof(DataFrameHeader) + sizeof(LlcHeader) + req.data->size();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_to_ds(1);

    hdr->addr1.Set(req.dst_addr.data());
    hdr->addr2.Set(req.src_addr.data());
    hdr->addr3.Set(req.dst_addr.data());

    SetSeqNo(hdr, &seq_);

    auto llc = packet->mut_field<LlcHeader>(sizeof(DataFrameHeader));
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = htobe16(kEapolProtocolId);
    std::memcpy(llc->payload, req.data->data(), req.data->size());

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send eapol request packet: %d\n", status);
        service::SendEapolResponse(device_, wlan_mlme::EapolResultCodes::TRANSMISSION_FAILURE);
        return status;
    }

    service::SendEapolResponse(device_, wlan_mlme::EapolResultCodes::SUCCESS);

    return status;
}

zx_status_t Station::HandleMlmeSetKeysReq(const wlan_mlme::SetKeysRequest& req) {
    debugfn();

    for (auto& keyDesc : *req.keylist) {
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
        memcpy(key_config.key, keyDesc.key->data(), keyDesc.length);
        key_config.key_type = key_type;
        key_config.key_len = static_cast<uint8_t>(keyDesc.length);
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
    controlled_port_ = PortState::kOpen;
    device_->SetStatus(ETH_STATUS_ONLINE);
    return ZX_OK;
}

zx_status_t Station::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    if (GetJoinChan().primary == GetDeviceChan().primary) {
        SetPowerManagementMode(true);
        // TODO(hahnr): start buffering tx packets (not here though)
    }
    return ZX_OK;
}

zx_status_t Station::PostChannelChange() {
    debugfn();
    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    if (GetJoinChan().primary == GetDeviceChan().primary) {
        SetPowerManagementMode(false);
        // TODO(hahnr): wait for TIM, and PS-POLL all buffered frames from AP.
    }
    return ZX_OK;
}

void Station::DumpDataFrame(const ImmutableDataFrame<LlcHeader>& frame) {
    const common::MacAddr& mymac = device_->GetState()->address();

    auto hdr = frame.hdr;

    auto is_ucast_to_self = mymac == hdr->addr1;
    auto is_mcast = hdr->addr1.IsBcast();
    auto is_bcast = hdr->addr1.IsMcast();
    auto is_interesting = is_ucast_to_self || is_mcast || is_bcast;

    auto associated = (state_ == WlanState::kAssociated);
    auto from_bss = (bssid() != nullptr && *bssid() == hdr->addr2);
    if (associated) { is_interesting = is_interesting && from_bss; }

    if (!is_interesting) { return; }

    auto llc = frame.body;
    auto frame_len = hdr->len() + frame.body_len;

    finspect("Inbound data frame: len %zu\n", frame_len);
    finspect("  wlan hdr: %s\n", debug::Describe(*hdr).c_str());
    finspect("  llc  hdr: %s\n", debug::Describe(*llc).c_str());
    finspect("  payload : %s\n", debug::HexDump(llc->payload, frame.body_len).c_str());
}

zx_status_t Station::SetPowerManagementMode(bool ps_mode) {
    if (state_ != WlanState::kAssociated) {
        warnf("cannot adjust power management before being associated\n");
        return ZX_OK;
    }

    const common::MacAddr& mymac = device_->GetState()->address();
    size_t len = sizeof(DataFrameHeader);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_subtype(DataSubtype::kNull);
    hdr->fc.set_pwr_mgmt(ps_mode);
    hdr->fc.set_to_ds(1);

    common::MacAddr bssid(bss_->bssid.data());
    hdr->addr1 = bssid;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid;

    SetSeqNo(hdr, &seq_);

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send power management packet: %d\n", status);
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

    const common::MacAddr& mymac = device_->GetState()->address();
    size_t len = sizeof(PsPollFrame);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto frame = packet->mut_field<PsPollFrame>(0);
    frame->fc.set_type(FrameType::kControl);
    frame->fc.set_subtype(ControlSubtype::kPsPoll);
    frame->aid = aid_;

    frame->bssid = common::MacAddr(bss_->bssid.data());
    frame->ta = mymac;

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send power management packet: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx::time Station::deadline_after_bcn_period(zx_duration_t tus) {
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    return timer_->Now() + WLAN_TU(bss_->beacon_period * tus);
}

bool Station::IsHTReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    return true;
}

bool Station::IsCbw40RxReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    return true;
}

bool Station::IsCbw40TxReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    // TODO(porce): Ralink dependency on BlockAck, AMPDU handling
    return false;
}

bool Station::IsQosReady() const {
    // TODO(NET-567,NET-599): Determine for each outbound data frame,
    // given the result of the dynamic capability negotiation, data frame
    // classification, and QoS policy.

    // Aruba / Ubiquiti are confirmed to be compatible with QoS field for the BlockAck session,
    // independently of 40MHz operation.
    return true;
}

HtCapabilities Station::BuildHtCapabilities() const {
    // TODO(porce): Find intersection of
    // - BSS capabilities
    // - Client radio capabilities
    // - Client configuration

    // Static cooking for Proof-of-Concept
    HtCapabilities htc;
    HtCapabilityInfo& hci = htc.ht_cap_info;

    hci.set_ldpc_coding_cap(0);  // Ralink RT5370 is incapable of LDPC.

    if (IsCbw40RxReady()) {
        hci.set_chan_width_set(HtCapabilityInfo::TWENTY_FORTY);
    } else {
        hci.set_chan_width_set(HtCapabilityInfo::TWENTY_ONLY);
    }

    hci.set_sm_power_save(HtCapabilityInfo::DISABLED);
    hci.set_greenfield(0);
    hci.set_short_gi_20(1);
    hci.set_short_gi_40(1);
    hci.set_tx_stbc(0);  // No plan to support STBC Tx
    hci.set_rx_stbc(1);  // one stream.
    hci.set_delayed_block_ack(0);
    hci.set_max_amsdu_len(HtCapabilityInfo::OCTETS_7935);  // Aruba
    // hci.set_max_amsdu_len(HtCapabilityInfo::OCTETS_3839);  // TP-Link
    hci.set_dsss_in_40(0);
    hci.set_intolerant_40(0);
    hci.set_lsig_txop_protect(0);

    AmpduParams& ampdu = htc.ampdu_params;
    ampdu.set_exponent(3);                                // 65535 bytes
    ampdu.set_min_start_spacing(AmpduParams::FOUR_USEC);  // Aruba
    // ampdu.set_min_start_spacing(AmpduParams::EIGHT_USEC);  // TP-Link
    // ampdu.set_min_start_spacing(AmpduParams::SIXTEEN_USEC);

    SupportedMcsSet& mcs = htc.mcs_set;
    mcs.rx_mcs_head.set_bitmask(0xff);  // MCS 0-7
    // mcs.rx_mcs_head.set_bitmask(0xffff);  // MCS 0-15
    mcs.tx_mcs.set_set_defined(1);  // Aruba

    HtExtCapabilities& hec = htc.ht_ext_cap;
    hec.set_pco(0);
    hec.set_pco_transition(HtExtCapabilities::PCO_RESERVED);
    hec.set_mcs_feedback(HtExtCapabilities::MCS_NOFEEDBACK);
    hec.set_htc_ht_support(0);
    hec.set_rd_responder(0);

    TxBfCapability& txbf = htc.txbf_cap;
    txbf.set_implicit_rx(0);
    txbf.set_rx_stag_sounding(0);
    txbf.set_tx_stag_sounding(0);
    txbf.set_rx_ndp(0);
    txbf.set_tx_ndp(0);
    txbf.set_implicit(0);
    txbf.set_calibration(TxBfCapability::CALIBRATION_NONE);
    txbf.set_csi(0);
    txbf.set_noncomp_steering(0);
    txbf.set_comp_steering(0);
    txbf.set_csi_feedback(TxBfCapability::FEEDBACK_NONE);
    txbf.set_noncomp_feedback(TxBfCapability::FEEDBACK_NONE);
    txbf.set_comp_feedback(TxBfCapability::FEEDBACK_NONE);
    txbf.set_min_grouping(TxBfCapability::MIN_GROUP_ONE);
    txbf.set_csi_antennas_human(1);           // 1 antenna
    txbf.set_noncomp_steering_ants_human(1);  // 1 antenna
    txbf.set_comp_steering_ants_human(1);     // 1 antenna
    txbf.set_csi_rows_human(1);               // 1 antenna
    txbf.set_chan_estimation_human(1);        // # space-time stream

    AselCapability& asel = htc.asel_cap;
    asel.set_asel(0);
    asel.set_csi_feedback_tx_asel(0);
    asel.set_explicit_csi_feedback(0);
    asel.set_antenna_idx_feedback(0);
    asel.set_rx_asel(0);
    asel.set_tx_sounding_ppdu(0);

    return htc;  // 28 bytes.
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

uint8_t Station::GetTid(const ImmutableBaseFrame<EthernetII>& frame) {
    return GetTid();
}
}  // namespace wlan
