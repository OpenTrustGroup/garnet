// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/dispatcher.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/channel.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <fuchsia/cpp/wlan_mlme.h>
#include <fuchsia/c/wlan_stats.h>

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

namespace {

template <unsigned int N, typename T> T align(T t) {
    static_assert(N > 1 && !(N & (N - 1)), "alignment must be with a power of 2");
    return (t + (N - 1)) & ~(N - 1);
}

}  // namespace

Dispatcher::Dispatcher(DeviceInterface* device, fbl::unique_ptr<Mlme> mlme)
    : device_(device), mlme_(std::move(mlme)) {
    debugfn();
    ZX_ASSERT(mlme_ != nullptr);
}

Dispatcher::~Dispatcher() {}

template <>
zx_status_t Dispatcher::HandleMlmeMethod<wlan_mlme::DeviceQueryRequest>(const Packet* packet,
                                                                        wlan_mlme::Method method);

zx_status_t Dispatcher::HandlePacket(const Packet* packet) {
    debugfn();

    ZX_DEBUG_ASSERT(packet != nullptr);
    ZX_DEBUG_ASSERT(packet->peer() != Packet::Peer::kUnknown);

    finspect("Packet: %s\n", debug::Describe(*packet).c_str());

    WLAN_STATS_INC(any_packet.in);

    // If there is no active MLME, block all packets but service ones.
    // MLME-JOIN.request and MLME-START.request implicitly select a mode and initialize the
    // MLME. DEVICE_QUERY.request is used to obtain device capabilities.

    auto service_msg = (packet->peer() == Packet::Peer::kService);
    if (mlme_ == nullptr && !service_msg) { return ZX_OK; }

    zx_status_t status = ZX_OK;
    switch (packet->peer()) {
    case Packet::Peer::kService:
        status = HandleSvcPacket(packet);
        break;
    case Packet::Peer::kEthernet:
        status = HandleEthPacket(packet);
        break;
    case Packet::Peer::kWlan: {
        auto fc = packet->field<FrameControl>(0);

        // TODO(porce): Handle HTC field.
        if (fc->HasHtCtrl()) {
            warnf("WLAN frame (type %u:%u) HTC field is present but not handled. Drop.", fc->type(),
                  fc->subtype());
            status = ZX_ERR_NOT_SUPPORTED;
            break;
        }

        switch (fc->type()) {
        case FrameType::kManagement:
            WLAN_STATS_INC(mgmt_frame.in);
            status = HandleMgmtPacket(packet);
            break;
        case FrameType::kControl:
            WLAN_STATS_INC(ctrl_frame.in);
            status = HandleCtrlPacket(packet);
            break;
        case FrameType::kData:
            WLAN_STATS_INC(data_frame.in);
            status = HandleDataPacket(packet);
            break;
        default:
            warnf("unknown MAC frame type %u\n", fc->type());
            status = ZX_ERR_NOT_SUPPORTED;
            break;
        }
        break;
    }
    default:
        break;
    }

    return status;
}

zx_status_t Dispatcher::HandlePortPacket(uint64_t key) {
    debugfn();
    ZX_DEBUG_ASSERT(ToPortKeyType(key) == PortKeyType::kMlme);

    ObjectId id(ToPortKeyId(key));
    switch (id.subtype()) {
    case to_enum_type(ObjectSubtype::kTimer): {
        auto status = mlme_->HandleTimeout(id);
        if (status == ZX_ERR_NOT_SUPPORTED) {
            warnf("unknown MLME timer target: %u\n", id.target());
        }
        break;
    }
    default:
        warnf("unknown MLME event subtype: %u\n", id.subtype());
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleCtrlPacket(const Packet* packet) {
    debugfn();

    if (packet->len() < sizeof(FrameControl)) {
        errorf("short control frame len=%zu\n", packet->len());
        return ZX_OK;
    }

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    auto fc = packet->field<FrameControl>(0);
    switch (fc->subtype()) {
    case ControlSubtype::kPsPoll: {
        if (packet->len() != sizeof(PsPollFrame)) {
            errorf("short ps poll frame len=%zu\n", packet->len());
            return ZX_OK;
        }
        auto ps_poll = reinterpret_cast<const PsPollFrame*>(packet->data());
        auto frame = ImmutableCtrlFrame<PsPollFrame>(ps_poll, nullptr, 0);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    default:
        debugf("rxed unfiltered control subtype 0x%02x\n", fc->subtype());
        return ZX_OK;
    }
}

zx_status_t Dispatcher::HandleDataPacket(const Packet* packet) {
    debugfn();

    auto hdr = packet->field<DataFrameHeader>(0);
    if (hdr == nullptr) {
        errorf("short data packet len=%zu\n", packet->len());
        return ZX_OK;
    }

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    switch (hdr->fc.subtype()) {
    case DataSubtype::kNull:
        // Fall-through
    case DataSubtype::kQosnull: {
        auto frame = ImmutableDataFrame<NilHeader>(hdr, nullptr, 0);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case DataSubtype::kDataSubtype:
        // Fall-through
    case DataSubtype::kQosdata:
        break;
    default:
        warnf("unsupported data subtype %02x\n", hdr->fc.subtype());
        return ZX_OK;
    }

    auto llc_offset = hdr->len();
    if (rxinfo->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4) {
        llc_offset = align<4>(llc_offset);
    }

    auto llc = packet->field<LlcHeader>(llc_offset);
    if (llc == nullptr) {
        errorf("short data packet len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    if (packet->len() < hdr->len() + sizeof(LlcHeader)) {
        errorf("short LLC packet len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    size_t llc_len = packet->len() - llc_offset;
    auto frame = ImmutableDataFrame<LlcHeader>(hdr, llc, llc_len);

    return mlme_->HandleFrame(frame, *rxinfo);
}

zx_status_t Dispatcher::HandleMgmtPacket(const Packet* packet) {
    debugfn();

    auto hdr = packet->field<MgmtFrameHeader>(0);
    if (hdr == nullptr) {
        errorf("short mgmt packet len=%zu\n", packet->len());
        return ZX_OK;
    }
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n", hdr->fc.val(), hdr->duration,
             hdr->sc.seq(), hdr->sc.frag());

    const common::MacAddr& dst = hdr->addr1;
    const common::MacAddr& src = hdr->addr2;
    const common::MacAddr& bssid = hdr->addr3;

    debughdr("dest: %s source: %s bssid: %s\n", MACSTR(dst), MACSTR(src), MACSTR(bssid));

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    size_t payload_len = packet->len() - hdr->len();

    switch (hdr->fc.subtype()) {
    case ManagementSubtype::kBeacon: {
        auto beacon = packet->field<Beacon>(hdr->len());
        if (beacon == nullptr) {
            errorf("beacon packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<Beacon>(hdr, beacon, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kProbeResponse: {
        auto proberesp = packet->field<ProbeResponse>(hdr->len());
        if (proberesp == nullptr) {
            errorf("probe response packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<ProbeResponse>(hdr, proberesp, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kProbeRequest: {
        auto probereq = packet->field<ProbeRequest>(hdr->len());
        if (probereq == nullptr) {
            errorf("probe request packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<ProbeRequest>(hdr, probereq, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAuthentication: {
        auto auth = packet->field<Authentication>(hdr->len());
        if (auth == nullptr) {
            errorf("authentication packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<Authentication>(hdr, auth, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kDeauthentication: {
        auto deauth = packet->field<Deauthentication>(hdr->len());
        if (deauth == nullptr) {
            errorf("deauthentication packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<Deauthentication>(hdr, deauth, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAssociationRequest: {
        auto authreq = packet->field<AssociationRequest>(hdr->len());
        if (authreq == nullptr) {
            errorf("assocation request packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<AssociationRequest>(hdr, authreq, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAssociationResponse: {
        auto authresp = packet->field<AssociationResponse>(hdr->len());
        if (authresp == nullptr) {
            errorf("assocation response packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<AssociationResponse>(hdr, authresp, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kDisassociation: {
        auto disassoc = packet->field<Disassociation>(hdr->len());
        if (disassoc == nullptr) {
            errorf("disassociation packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<Disassociation>(hdr, disassoc, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAction: {
        auto action = packet->field<ActionFrame>(hdr->len());
        if (action == nullptr) {
            errorf("action packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        if (!hdr->IsAction()) {
            errorf("action packet is not an action\n");
            return ZX_ERR_IO;
        }
        HandleActionPacket(packet, hdr, action, rxinfo);
    }
    default:
        if (!dst.IsBcast()) {
            // TODO(porce): Evolve this logic to support AP role.
            debugf("Rxed Mgmt frame (type: %d) but not handled\n", hdr->fc.subtype());
        }
        break;
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleActionPacket(const Packet* packet, const MgmtFrameHeader* hdr,
                                           const ActionFrame* action,
                                           const wlan_rx_info_t* rxinfo) {
    if (action->category != action::Category::kBlockAck) {
        verbosef("Rxed Action frame with category %d. Not handled.\n", action->category);
        return ZX_OK;
    }

    size_t payload_len = packet->len() - hdr->len();
    auto ba_frame = packet->field<ActionFrameBlockAck>(hdr->len());
    if (ba_frame == nullptr) {
        errorf("bloackack packet too small (len=%zd)\n", payload_len);
        return ZX_ERR_IO;
    }

    switch (ba_frame->action) {
    case action::BaAction::kAddBaRequest: {
        auto addbar = packet->field<AddBaRequestFrame>(hdr->len());
        if (addbar == nullptr) {
            errorf("addbar packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }

        // TODO(porce): Support AddBar. Work with lower mac.
        // TODO(porce): Make this conditional depending on the hardware capability.

        auto frame = ImmutableMgmtFrame<AddBaRequestFrame>(hdr, addbar, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
        break;
    }
    case action::BaAction::kAddBaResponse: {
        auto addba_resp = packet->field<AddBaResponseFrame>(hdr->len());
        if (addba_resp == nullptr) {
            errorf("addba_resp packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = ImmutableMgmtFrame<AddBaResponseFrame>(hdr, addba_resp, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
        break;
    }
    case action::BaAction::kDelBa:
    // fall-through
    default:
        warnf("BlockAck action frame with action %u not handled.\n", ba_frame->action);
        break;
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleEthPacket(const Packet* packet) {
    debugfn();

    auto hdr = packet->field<EthernetII>(0);
    if (hdr == nullptr) {
        errorf("short ethernet frame len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }

    auto payload = packet->field<uint8_t>(sizeof(EthernetII));
    size_t payload_len = packet->len() - sizeof(EthernetII);
    auto frame = ImmutableBaseFrame<EthernetII>(hdr, payload, payload_len);
    return mlme_->HandleFrame(frame);
}

zx_status_t Dispatcher::HandleSvcPacket(const Packet* packet) {
    debugfn();

    const uint8_t* bytes = packet->data();
    auto hdr = FromBytes<fidl_message_header_t>(bytes, packet->len());
    if (hdr == nullptr) {
        errorf("short service packet len=%zu\n", packet->len());
        return ZX_OK;
    }
    debughdr("service packet txid=%u flags=%u ordinal=%u\n", hdr->txid, hdr->flags, hdr->ordinal);

    auto method = static_cast<wlan_mlme::Method>(hdr->ordinal);

    if (method == wlan_mlme::Method::DEVICE_QUERY_request) {
        return HandleMlmeMethod<wlan_mlme::DeviceQueryRequest>(packet, method);
    }

    switch (method) {
    case wlan_mlme::Method::RESET_request:
        infof("resetting MLME\n");
        HandleMlmeMethod<wlan_mlme::ResetRequest>(packet, method);
        return ZX_OK;
    case wlan_mlme::Method::START_request:
        return HandleMlmeMethod<wlan_mlme::StartRequest>(packet, method);
    case wlan_mlme::Method::STOP_request:
        return HandleMlmeMethod<wlan_mlme::StopRequest>(packet, method);
    case wlan_mlme::Method::SCAN_request:
        return HandleMlmeMethod<wlan_mlme::ScanRequest>(packet, method);
    case wlan_mlme::Method::JOIN_request:
        return HandleMlmeMethod<wlan_mlme::JoinRequest>(packet, method);
    case wlan_mlme::Method::AUTHENTICATE_request:
        return HandleMlmeMethod<wlan_mlme::AuthenticateRequest>(packet, method);
    case wlan_mlme::Method::AUTHENTICATE_response:
        return HandleMlmeMethod<wlan_mlme::AuthenticateResponse>(packet, method);
    case wlan_mlme::Method::DEAUTHENTICATE_request:
        return HandleMlmeMethod<wlan_mlme::DeauthenticateRequest>(packet, method);
    case wlan_mlme::Method::ASSOCIATE_request:
        return HandleMlmeMethod<wlan_mlme::AssociateRequest>(packet, method);
    case wlan_mlme::Method::ASSOCIATE_response:
        return HandleMlmeMethod<wlan_mlme::AssociateResponse>(packet, method);
    case wlan_mlme::Method::EAPOL_request:
        return HandleMlmeMethod<wlan_mlme::EapolRequest>(packet, method);
    case wlan_mlme::Method::SETKEYS_request:
        return HandleMlmeMethod<wlan_mlme::SetKeysRequest>(packet, method);
    default:
        warnf("unknown MLME method %u\n", hdr->ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

template <typename Message>
zx_status_t Dispatcher::HandleMlmeMethod(const Packet* packet, wlan_mlme::Method method) {
    Message msg;
    auto status = DeserializeServiceMsg<Message>(*packet, method, &msg);
    if (status != ZX_OK) {
        errorf("could not deserialize MLME Method %d: %d\n", method, status);
        return status;
    }
    return mlme_->HandleFrame(method, msg);
}

template <>
zx_status_t Dispatcher::HandleMlmeMethod<wlan_mlme::DeviceQueryRequest>(const Packet* unused_packet,
                                                                        wlan_mlme::Method method) {
    debugfn();
    ZX_DEBUG_ASSERT(method == wlan_mlme::Method::DEVICE_QUERY_request);

    wlan_mlme::DeviceQueryConfirm resp;
    const wlanmac_info_t& info = device_->GetWlanInfo();

    memcpy(resp.mac_addr.mutable_data(), info.eth_info.mac, ETH_MAC_SIZE);

    switch (info.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
        resp.role = wlan_mlme::MacRole::CLIENT;
        break;
    case WLAN_MAC_ROLE_AP:
        resp.role = wlan_mlme::MacRole::AP;
        break;
    default:
        // TODO(tkilbourn): return an error?
        break;
    }

    resp.bands->resize(0);
    for (uint8_t band_idx = 0; band_idx < info.num_bands; band_idx++) {
        const wlan_band_info_t& band_info = info.bands[band_idx];
        wlan_mlme::BandCapabilities band;
        band.basic_rates->resize(0);
        for (size_t rate_idx = 0; rate_idx < sizeof(band_info.basic_rates); rate_idx++) {
            if (band_info.basic_rates[rate_idx] != 0) {
                band.basic_rates->push_back(band_info.basic_rates[rate_idx]);
            }
        }
        const wlan_chan_list_t& chan_list = band_info.supported_channels;
        band.base_frequency = chan_list.base_freq;
        band.channels->resize(0);
        for (size_t chan_idx = 0; chan_idx < sizeof(chan_list.channels); chan_idx++) {
            if (chan_list.channels[chan_idx] != 0) {
                band.channels->push_back(chan_list.channels[chan_idx]);
            }
        }
        resp.bands->push_back(std::move(band));
    }

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    // size_t buf_len = sizeof(fidl_message_header_t) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status =
        SerializeServiceMsg(packet.get(), wlan_mlme::Method::DEVICE_QUERY_confirm, &resp);
    if (status != ZX_OK) {
        errorf("could not serialize DeviceQueryResponse: %d\n", status);
        return status;
    }

    return device_->SendService(std::move(packet));
}

zx_status_t Dispatcher::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    mlme_->PreChannelChange(chan);
    return ZX_OK;
}

zx_status_t Dispatcher::PostChannelChange() {
    debugfn();
    mlme_->PostChannelChange();
    return ZX_OK;
}

void Dispatcher::HwIndication(uint32_t ind) {
    debugfn();
    mlme_->HwIndication(ind);
}

}  // namespace wlan
