// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/c/fidl.h>

namespace wlan {
namespace service {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

zx_status_t SendJoinConfirm(DeviceInterface* device, wlan_mlme::JoinResultCodes result_code) {
    debugfn();
    wlan_mlme::JoinConfirm conf;
    conf.result_code = result_code;
    return SendServiceMsg(device, &conf, fuchsia_wlan_mlme_MLMEJoinConfOrdinal);
}

zx_status_t SendAuthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta,
                            wlan_mlme::AuthenticateResultCodes code) {
    debugfn();
    wlan_mlme::AuthenticateConfirm conf;
    peer_sta.CopyTo(conf.peer_sta_address.mutable_data());
    // TODO(tkilbourn): set this based on the actual auth type
    conf.auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;
    conf.result_code = code;
    return SendServiceMsg(device, &conf, fuchsia_wlan_mlme_MLMEAuthenticateConfOrdinal);
}

zx_status_t SendAuthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                               wlan_mlme::AuthenticationTypes auth_type) {
    debugfn();
    wlan_mlme::AuthenticateIndication ind;
    peer_sta.CopyTo(ind.peer_sta_address.mutable_data());
    ind.auth_type = auth_type;
    return SendServiceMsg(device, &ind, fuchsia_wlan_mlme_MLMEAuthenticateIndOrdinal);
}

zx_status_t SendDeauthConfirm(DeviceInterface* device, const common::MacAddr& peer_sta) {
    debugfn();
    wlan_mlme::DeauthenticateConfirm conf;
    peer_sta.CopyTo(conf.peer_sta_address.mutable_data());
    return SendServiceMsg(device, &conf, fuchsia_wlan_mlme_MLMEDeauthenticateConfOrdinal);
}

zx_status_t SendDeauthIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                 wlan_mlme::ReasonCode code) {
    debugfn();
    wlan_mlme::DeauthenticateIndication ind;
    peer_sta.CopyTo(ind.peer_sta_address.mutable_data());
    ind.reason_code = code;
    return SendServiceMsg(device, &ind, fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal);
}

zx_status_t SendAssocConfirm(DeviceInterface* device, wlan_mlme::AssociateResultCodes code,
                             uint16_t aid) {
    debugfn();
    ZX_DEBUG_ASSERT(code != wlan_mlme::AssociateResultCodes::SUCCESS || aid != 0);

    wlan_mlme::AssociateConfirm conf;
    conf.result_code = code;
    conf.association_id = aid;
    return SendServiceMsg(device, &conf, fuchsia_wlan_mlme_MLMEAssociateConfOrdinal);
}

zx_status_t SendAssocIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                uint16_t listen_interval, const SsidElement& ssid_element,
                                const RsnElement* rsn_elem) {
    debugfn();
    wlan_mlme::AssociateIndication ind;
    peer_sta.CopyTo(ind.peer_sta_address.mutable_data());
    ind.listen_interval = listen_interval;
    ind.ssid = ::fidl::VectorPtr<uint8_t>::New(ssid_element.body_len());
    std::memcpy(ind.ssid->data(), ssid_element.ssid, ssid_element.body_len());

    if (rsn_elem != nullptr) {
        std::vector<uint8_t> rsne_raw;
        rsne_raw.reserve(rsn_elem->len());
        rsne_raw.emplace_back(rsn_elem->hdr.id);
        rsne_raw.emplace_back(rsn_elem->hdr.len);
        rsne_raw.emplace_back(static_cast<uint8_t>(rsn_elem->version & 0xffu));
        rsne_raw.emplace_back(static_cast<uint8_t>(rsn_elem->version >> 8u));
        rsne_raw.insert(rsne_raw.end(), rsn_elem->fields,
                        rsn_elem->fields + rsn_elem->body_len() - sizeof(rsn_elem->version));
        ind.rsn.reset(std::move(rsne_raw));
    } else {
        ind.rsn.reset();
    }
    return SendServiceMsg(device, &ind, fuchsia_wlan_mlme_MLMEAssociateIndOrdinal);
}

zx_status_t SendDisassociateIndication(DeviceInterface* device, const common::MacAddr& peer_sta,
                                       uint16_t code) {
    debugfn();
    wlan_mlme::DisassociateIndication ind;
    peer_sta.CopyTo(ind.peer_sta_address.mutable_data());
    ind.reason_code = code;
    return SendServiceMsg(device, &ind, fuchsia_wlan_mlme_MLMEDisassociateIndOrdinal);
}

zx_status_t SendSignalReportIndication(DeviceInterface* device, common::dBm rssi_dbm) {
    debugfn();
    wlan_mlme::SignalReportIndication ind;
    ind.rssi_dbm = rssi_dbm.val;
    return SendServiceMsg(device, &ind, fuchsia_wlan_mlme_MLMESignalReportOrdinal);
}

zx_status_t SendEapolConfirm(DeviceInterface* device, wlan_mlme::EapolResultCodes result_code) {
    debugfn();
    wlan_mlme::EapolConfirm resp;
    resp.result_code = result_code;
    return SendServiceMsg(device, &resp, fuchsia_wlan_mlme_MLMEEapolConfOrdinal);
}

zx_status_t SendEapolIndication(DeviceInterface* device, const EapolHdr& eapol,
                                const common::MacAddr& src, const common::MacAddr& dst) {
    debugfn();

    // Limit EAPOL packet size. The EAPOL packet's size depends on the link transport protocol and
    // might exceed 255 octets. However, we don't support EAP yet and EAPOL Key frames are always
    // shorter.
    // TODO(hahnr): If necessary, find a better upper bound once we support EAP.
    size_t frame_len = eapol.len() + eapol.get_packet_body_length();
    if (frame_len > 255) { return ZX_OK; }

    wlan_mlme::EapolIndication ind;
    ind.data = ::fidl::VectorPtr<uint8_t>::New(frame_len);
    std::memcpy(ind.data->data(), &eapol, frame_len);
    src.CopyTo(ind.src_addr.mutable_data());
    dst.CopyTo(ind.dst_addr.mutable_data());
    return SendServiceMsg(device, &ind, fuchsia_wlan_mlme_MLMEEapolIndOrdinal);
}

}  // namespace service
}  // namespace wlan
