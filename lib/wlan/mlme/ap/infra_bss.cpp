// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/infra_bss.h>

#include <wlan/common/channel.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <zircon/syscalls.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

InfraBss::InfraBss(DeviceInterface* device, fbl::unique_ptr<BeaconSender> bcn_sender,
                   const common::MacAddr& bssid)
    : bssid_(bssid), device_(device), bcn_sender_(fbl::move(bcn_sender)) {
    ZX_DEBUG_ASSERT(bcn_sender_ != nullptr);
}

InfraBss::~InfraBss() {
    // The BSS should always be explicitly stopped.
    // Throw in debug builds, stop in release ones.
    ZX_DEBUG_ASSERT(!IsStarted());

    // Ensure BSS is stopped correctly.
    Stop();
}

void InfraBss::Start(const MlmeMsg<wlan_mlme::StartRequest>& req) {
    if (IsStarted()) { return; }

    // Move to requested channel.
    auto chan = wlan_channel_t{
        .primary = req.body()->channel,
        .cbw = CBW20,
    };

    if (IsCbw40RxReady()) {
        wlan_channel_t chan_override = chan;
        chan_override.cbw = CBW40;
        chan.cbw = common::GetValidCbw(chan_override);
    }

    auto status = device_->SetChannel(chan);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] requested start on channel %u failed: %d\n",
               bssid_.ToString().c_str(), req.body()->channel, status);
    }
    chan_ = chan;

    ZX_DEBUG_ASSERT(req.body()->dtim_period > 0);
    if (req.body()->dtim_period == 0) {
        ps_cfg_.SetDtimPeriod(1);
        warnf(
            "[infra-bss] [%s] received start request with reserved DTIM period of "
            "0; falling back "
            "to DTIM period of 1\n",
            bssid_.ToString().c_str());
    } else {
        ps_cfg_.SetDtimPeriod(req.body()->dtim_period);
    }

    debugbss("[infra-bss] [%s] starting BSS\n", bssid_.ToString().c_str());
    debugbss("    SSID: \"%s\"\n", debug::ToAsciiOrHexStr(*req.body()->ssid).c_str());
    debugbss("    Beacon Period: %u\n", req.body()->beacon_period);
    debugbss("    DTIM Period: %u\n", req.body()->dtim_period);
    debugbss("    Channel: %u\n", req.body()->channel);

    // Keep track of start request which holds important configuration
    // information.
    req.body()->Clone(&start_req_);

    // Start sending Beacon frames.
    started_at_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
    bcn_sender_->Start(this, ps_cfg_, req);
}

void InfraBss::Stop() {
    if (!IsStarted()) { return; }

    debugbss("[infra-bss] [%s] stopping BSS\n", bssid_.ToString().c_str());

    clients_.clear();
    bcn_sender_->Stop();
    started_at_ = 0;
}

bool InfraBss::IsStarted() {
    return started_at_ != 0;
}

void InfraBss::HandleAnyFrame(fbl::unique_ptr<Packet> pkt) {
    switch (pkt->peer()) {
    case Packet::Peer::kEthernet: {
        if (auto eth_frame = EthFrameView::CheckType(pkt.get()).CheckLength()) {
            HandleEthFrame(eth_frame.IntoOwned(fbl::move(pkt)));
        }
        break;
    }
    case Packet::Peer::kWlan:
        HandleAnyWlanFrame(fbl::move(pkt));
        break;
    default:
        errorf("unknown Packet peer: %u\n", pkt->peer());
        break;
    }
}

void InfraBss::HandleAnyWlanFrame(fbl::unique_ptr<Packet> pkt) {
    if (auto possible_mgmt_frame = MgmtFrameView<>::CheckType(pkt.get())) {
        if (auto mgmt_frame = possible_mgmt_frame.CheckLength()) {
            HandleAnyMgmtFrame(mgmt_frame.IntoOwned(fbl::move(pkt)));
        }
    } else if (auto possible_data_frame = DataFrameView<>::CheckType(pkt.get())) {
        if (auto data_frame = possible_data_frame.CheckLength()) {
            HandleAnyDataFrame(data_frame.IntoOwned(fbl::move(pkt)));
        }
    } else if (auto possible_ctrl_frame = CtrlFrameView<>::CheckType(pkt.get())) {
        if (auto ctrl_frame = possible_ctrl_frame.CheckLength()) {
            HandleAnyCtrlFrame(ctrl_frame.IntoOwned(fbl::move(pkt)));
        }
    }
}

void InfraBss::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    auto mgmt_frame = frame.View();
    bool to_bss = (bssid_ == mgmt_frame.hdr()->addr1 && bssid_ == mgmt_frame.hdr()->addr3);

    // Special treatment for ProbeRequests which can be addressed towards
    // broadcast address.
    if (auto possible_probe_req_frame = mgmt_frame.CheckBodyType<ProbeRequest>()) {
        if (auto probe_req_frame = possible_probe_req_frame.CheckLength()) {
            // Drop all ProbeRequests which are neither targeted to this BSS nor to
            // broadcast address.
            auto hdr = probe_req_frame.hdr();
            bool to_bcast = hdr->addr1.IsBcast() && hdr->addr3.IsBcast();
            if (!to_bss && !to_bcast) { return; }

            // Valid ProbeRequest, let BeaconSender process and respond to it.
            bcn_sender_->SendProbeResponse(probe_req_frame);
            return;
        }
    }

    // Drop management frames which are not targeted towards this BSS.
    if (!to_bss) { return; }

    // Register the client if it's not yet known.
    const auto& client_addr = mgmt_frame.hdr()->addr2;
    if (!HasClient(client_addr)) {
        if (auto auth_frame = mgmt_frame.CheckBodyType<Authentication>().CheckLength()) {
            HandleNewClientAuthAttempt(auth_frame);
        }
    }

    // Forward all frames to the correct client.
    auto client = GetClient(client_addr);
    if (client != nullptr) { client->HandleAnyMgmtFrame(fbl::move(frame)); }
}

void InfraBss::HandleAnyDataFrame(DataFrame<>&& frame) {
    if (bssid_ != frame.hdr()->addr1) { return; }

    // Let the correct RemoteClient instance process the received frame.
    const auto& client_addr = frame.hdr()->addr2;
    auto client = GetClient(client_addr);
    if (client != nullptr) { client->HandleAnyDataFrame(fbl::move(frame)); }
}

void InfraBss::HandleAnyCtrlFrame(CtrlFrame<>&& frame) {
    auto ctrl_frame = frame.View();

    if (auto pspoll_frame = ctrl_frame.CheckBodyType<PsPollFrame>().CheckLength()) {
        if (pspoll_frame.body()->bssid != bssid_) { return; }

        const auto& client_addr = pspoll_frame.body()->ta;
        auto client = GetClient(client_addr);
        if (client == nullptr) { return; }
        if (client->GetAid() != pspoll_frame.body()->aid) { return; }

        client->HandleAnyCtrlFrame(fbl::move(frame));
    }
}

zx_status_t InfraBss::HandleTimeout(const common::MacAddr& client_addr) {
    auto client = GetClient(client_addr);
    ZX_DEBUG_ASSERT(client != nullptr);
    if (client != nullptr) { client->HandleTimeout(); }
    return ZX_OK;
}

void InfraBss::HandleEthFrame(EthFrame&& frame) {
    // Lookup client associated with incoming unicast frame.
    auto& dest_addr = frame.hdr()->dest;
    if (dest_addr.IsUcast()) {
        auto client = GetClient(dest_addr);
        if (client != nullptr) { client->HandleAnyEthFrame(fbl::move(frame)); }
        return;
    }

    // Process multicast frames ourselves.
    fbl::unique_ptr<Packet> out_frame;
    auto status = EthToDataFrame(frame, &out_frame);
    if (status == ZX_OK) {
        SendDataFrame(fbl::move(out_frame));
    } else {
        errorf("[infra-bss] [%s] couldn't convert ethernet frame: %d\n", bssid_.ToString().c_str(),
               status);
    }
}

zx_status_t InfraBss::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    common::MacAddr peer_addr;
    if (auto auth_resp = msg.As<wlan_mlme::AuthenticateResponse>()) {
        peer_addr = common::MacAddr(auth_resp->body()->peer_sta_address.data());
    } else if (auto assoc_resp = msg.As<wlan_mlme::AssociateResponse>()) {
        peer_addr = common::MacAddr(assoc_resp->body()->peer_sta_address.data());
    } else if (auto eapol_req = msg.As<wlan_mlme::EapolRequest>()) {
        peer_addr = common::MacAddr(eapol_req->body()->dst_addr.data());
    } else {
        warnf("[infra-bss] received unsupported MLME msg; ordinal: %u\n", msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }

    auto client = GetClient(peer_addr);
    ZX_DEBUG_ASSERT(client != nullptr);
    if (client != nullptr) {
        return client->HandleMlmeMsg(msg);
    } else {
        warnf("[infra-bss] unrecognized peer address in MlmeMsg: %s -- ordinal: %u\n",
              peer_addr.ToString().c_str(), msg.ordinal());
    }

    return ZX_OK;
}

void InfraBss::HandleNewClientAuthAttempt(const MgmtFrameView<Authentication>& frame) {
    auto& client_addr = frame.hdr()->addr2;
    ZX_DEBUG_ASSERT(!HasClient(client_addr));

    debugbss("[infra-bss] [%s] new client: %s\n", bssid_.ToString().c_str(),
             client_addr.ToString().c_str());

    // Else, create a new remote client instance.
    fbl::unique_ptr<Timer> timer = nullptr;
    auto status = CreateClientTimer(client_addr, &timer);
    if (status == ZX_OK) {
        auto client = fbl::make_unique<RemoteClient>(device_, fbl::move(timer),
                                                     this,  // bss
                                                     this,  // client listener
                                                     client_addr);
        clients_.emplace(client_addr, fbl::move(client));
    } else {
        errorf("[infra-bss] [%s] could not create client timer: %d\n", bssid_.ToString().c_str(),
               status);
    }
}

zx_status_t InfraBss::HandleClientDeauth(const common::MacAddr& client_addr) {
    debugfn();
    auto iter = clients_.find(client_addr);
    ZX_DEBUG_ASSERT(iter != clients_.end());
    if (iter == clients_.end()) {
        errorf("[infra-bss] [%s] unknown client deauthenticated: %s\n", bssid_.ToString().c_str(),
               client_addr.ToString().c_str());
        return ZX_ERR_BAD_STATE;
    }

    debugbss("[infra-bss] [%s] removing client %s\n", bssid_.ToString().c_str(),
             client_addr.ToString().c_str());
    clients_.erase(iter);
    return ZX_OK;
}

void InfraBss::HandleClientDisassociation(aid_t aid) {
    debugfn();
    ps_cfg_.GetTim()->SetTrafficIndication(aid, false);
}

void InfraBss::HandleClientBuChange(const common::MacAddr& client_addr, size_t bu_count) {
    debugfn();
    auto client = GetClient(client_addr);
    ZX_DEBUG_ASSERT(client != nullptr);
    if (client == nullptr) {
        errorf("[infra-bss] [%s] received traffic indication for untracked client: %s\n",
               bssid_.ToString().c_str(), client_addr.ToString().c_str());
        return;
    }
    auto aid = client->GetAid();
    ZX_DEBUG_ASSERT(aid != kUnknownAid);
    if (aid == kUnknownAid) {
        errorf(
            "[infra-bss] [%s] received traffic indication from client with unknown "
            "AID: %s\n",
            bssid_.ToString().c_str(), client_addr.ToString().c_str());
        return;
    }

    ps_cfg_.GetTim()->SetTrafficIndication(aid, bu_count > 0);
}

bool InfraBss::HasClient(const common::MacAddr& client) {
    return clients_.find(client) != clients_.end();
}

RemoteClientInterface* InfraBss::GetClient(const common::MacAddr& addr) {
    auto iter = clients_.find(addr);
    if (iter == clients_.end()) { return nullptr; }
    return iter->second.get();
}

zx_status_t InfraBss::CreateClientTimer(const common::MacAddr& client_addr,
                                        fbl::unique_ptr<Timer>* out_timer) {
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kBss));
    timer_id.set_mac(client_addr.ToU64());
    zx_status_t status =
        device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), out_timer);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] could not create bss timer: %d\n", bssid_.ToString().c_str(),
               status);
        return status;
    }
    return ZX_OK;
}

bool InfraBss::ShouldBufferFrame(const common::MacAddr& receiver_addr) const {
    // Buffer non-GCR-SP frames when at least one client is dozing.
    // Note: Currently group addressed service transmission is not supported and
    // thus, every group message should get buffered.
    return receiver_addr.IsGroupAddr() && ps_cfg_.GetTim()->HasDozingClients();
}

zx_status_t InfraBss::BufferFrame(fbl::unique_ptr<Packet> packet) {
    // Drop oldest frame if queue reached its limit.
    if (bu_queue_.size() >= kMaxGroupAddressedBu) {
        bu_queue_.Dequeue();
        warnf("[infra-bss] [%s] dropping oldest group addressed frame\n",
              bssid_.ToString().c_str());
    }

    debugps("[infra-bss] [%s] buffer outbound frame\n", bssid_.ToString().c_str());
    bu_queue_.Enqueue(fbl::move(packet));
    ps_cfg_.GetTim()->SetTrafficIndication(kGroupAdressedAid, true);
    return ZX_OK;
}

zx_status_t InfraBss::SendDataFrame(fbl::unique_ptr<Packet> packet) {
    ZX_DEBUG_ASSERT(packet->len() >= sizeof(DataFrameHeader));
    if (packet->len() < sizeof(DataFrameHeader)) { return ZX_ERR_INVALID_ARGS; }

    auto hdr = packet->field<DataFrameHeader>(0);
    if (ShouldBufferFrame(hdr->addr1)) { return BufferFrame(fbl::move(packet)); }

    return device_->SendWlan(fbl::move(packet));
}

zx_status_t InfraBss::SendMgmtFrame(fbl::unique_ptr<Packet> packet) {
    ZX_DEBUG_ASSERT(packet->len() >= sizeof(MgmtFrameHeader));
    if (packet->len() < sizeof(MgmtFrameHeader)) { return ZX_ERR_INVALID_ARGS; }

    auto hdr = packet->field<MgmtFrameHeader>(0);
    if (ShouldBufferFrame(hdr->addr1)) { return BufferFrame(fbl::move(packet)); }

    return device_->SendWlan(fbl::move(packet));
}

zx_status_t InfraBss::SendEthFrame(fbl::unique_ptr<Packet> packet) {
    return device_->SendEthernet(fbl::move(packet));
}

zx_status_t InfraBss::SendNextBu() {
    ZX_DEBUG_ASSERT(bu_queue_.size() > 0);
    if (bu_queue_.size() == 0) { return ZX_ERR_BAD_STATE; }

    auto packet = bu_queue_.Dequeue();
    ZX_DEBUG_ASSERT(packet->len() > sizeof(FrameControl));
    if (packet->len() < sizeof(FrameControl)) { return ZX_ERR_BAD_STATE; }

    // Set `more` bit if there are more BU available.
    // IEEE Std 802.11-2016, 9.2.4.1.8
    auto fc = packet->mut_field<FrameControl>(0);
    fc->set_more_data(bu_queue_.size() > 0 ? 1 : 0);

    debugps("[infra-bss] [%s] sent group addressed BU\n", bssid_.ToString().c_str());
    return device_->SendWlan(fbl::move(packet));
}

zx_status_t InfraBss::EthToDataFrame(const EthFrame& frame, fbl::unique_ptr<Packet>* out_packet) {
    size_t max_frame_len = DataFrameHeader::max_len() + LlcHeader::max_len() + frame.body_len();
    auto packet = GetWlanPacket(max_frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.clear();
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_subtype(DataSubtype::kDataSubtype);
    hdr->fc.set_from_ds(1);
    // TODO(hahnr): Protect outgoing frames when RSNA is established.
    hdr->addr1 = frame.hdr()->dest;
    hdr->addr2 = bssid_;
    hdr->addr3 = frame.hdr()->src;

    hdr->sc.set_seq(NextSeq(*hdr));

    wlan_tx_info_t txinfo = {
        // TODO(porce): Determine PHY and CBW based on the association
        // negotiation.
        .tx_flags = 0x0,
        .valid_fields =
            WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH | WLAN_TX_INFO_VALID_MCS,
        .phy = WLAN_PHY_HT,
        .cbw = CBW20,
        //.date_rate = 0x0,
        .mcs = 0x7,
    };

    if (IsCbw40TxReady()) {
        // Ralink appears to setup BlockAck session AND AMPDU handling
        // TODO(porce): Use a separate sequence number space in that case
        if (hdr->addr3.IsUcast()) {
            // 40MHz direction does not matter here.
            // Radio uses the operational channel setting. This indicates the
            // bandwidth without direction.
            txinfo.cbw = CBW40;
        }
    }

    auto llc = packet->mut_field<LlcHeader>(hdr->len());
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = frame.hdr()->ether_type;
    std::memcpy(llc->payload, frame.body(), frame.body_len());

    auto frame_len = hdr->len() + llc->len() + frame.body_len();
    auto status = packet->set_len(frame_len);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] could not set data frame length to %zu: %d\n",
               bssid_.ToString().c_str(), frame_len, status);
        return status;
    }

    finspect("Outbound data frame: len %zu, hdr_len:%zu body_len:%zu frame_len:%zu\n",
             packet->len(), hdr->len(), frame.body_len(), frame_len);
    finspect("  wlan hdr: %s\n", debug::Describe(*hdr).c_str());
    finspect("  llc  hdr: %s\n", debug::Describe(*llc).c_str());
    finspect("  frame   : %s\n", debug::HexDump(packet->data(), frame_len).c_str());

    packet->CopyCtrlFrom(txinfo);
    *out_packet = fbl::move(packet);

    return ZX_OK;
}

void InfraBss::OnPreTbtt() {
    bcn_sender_->UpdateBeacon(ps_cfg_);
    ps_cfg_.NextDtimCount();
}

void InfraBss::OnBcnTxComplete() {
    // Only send out multicast frames if the Beacon we just sent was a DTIM.
    if (ps_cfg_.LastDtimCount() != 0) { return; }
    if (bu_queue_.size() == 0) { return; }

    debugps("[infra-bss] [%s] sending %zu group addressed BU\n", bssid_.ToString().c_str(),
            bu_queue_.size());
    while (bu_queue_.size() > 0) {
        auto status = SendNextBu();
        if (status != ZX_OK) {
            errorf("[infra-bss] [%s] could not send group addressed BU: %d\n",
                   bssid_.ToString().c_str(), status);
            return;
        }
    }

    ps_cfg_.GetTim()->SetTrafficIndication(kGroupAdressedAid, false);
}

const common::MacAddr& InfraBss::bssid() const {
    return bssid_;
}

uint64_t InfraBss::timestamp() {
    zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    zx_duration_t uptime_ns = now - started_at_;
    return uptime_ns / 1000;  // as microseconds
}

seq_t InfraBss::NextSeq(const MgmtFrameHeader& hdr) {
    return NextSeqNo(hdr, &seq_);
}

seq_t InfraBss::NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) {
    return NextSeqNo(hdr, aci, &seq_);
}

seq_t InfraBss::NextSeq(const DataFrameHeader& hdr) {
    return NextSeqNo(hdr, &seq_);
}

bool InfraBss::IsRsn() const {
    return !start_req_.rsne.is_null();
}

bool InfraBss::IsHTReady() const {
    // TODO(NET-567): Reflect hardware capabilities and association negotiation
    return true;
}

bool InfraBss::IsCbw40RxReady() const {
    // TODO(NET-567): Reflect hardware capabilities and association negotiation
    return true;
}

bool InfraBss::IsCbw40TxReady() const {
    // TODO(NET-567): Reflect hardware capabilities and association negotiation
    return false;
}

HtCapabilities InfraBss::BuildHtCapabilities() const {
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

HtOperation InfraBss::BuildHtOperation(const wlan_channel_t& chan) const {
    // TODO(porce): Query the BSS internal data to fill up the parameters.
    HtOperation hto;

    hto.primary_chan = chan.primary;
    HtOpInfoHead& head = hto.head;

    switch (chan.cbw) {
    case CBW40ABOVE:
        head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_ABOVE);
        head.set_sta_chan_width(HtOpInfoHead::ANY);
        break;
    case CBW40BELOW:
        head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_BELOW);
        head.set_sta_chan_width(HtOpInfoHead::ANY);
        break;
    case CBW20:
    default:
        head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_NONE);
        head.set_sta_chan_width(HtOpInfoHead::TWENTY);
        break;
    }

    head.set_rifs_mode(0);
    head.set_reserved1(0);  // TODO(porce): Tweak this for 802.11n D1.10 compatibility
    head.set_ht_protect(HtOpInfoHead::NONE);
    head.set_nongreenfield_present(1);
    head.set_reserved2(0);  // TODO(porce): Tweak this for 802.11n D1.10 compatibility
    head.set_obss_non_ht(0);
    head.set_center_freq_seg2(0);
    head.set_dual_beacon(0);
    head.set_dual_cts_protect(0);

    HtOpInfoTail& tail = hto.tail;
    tail.set_stbc_beacon(0);
    tail.set_lsig_txop_protect(0);
    tail.set_pco_active(0);
    tail.set_pco_phase(0);

    SupportedMcsSet& mcs = hto.basic_mcs_set;
    mcs.rx_mcs_head.set_bitmask(0xff);  // MCS 0-7

    return hto;
}

}  // namespace wlan
