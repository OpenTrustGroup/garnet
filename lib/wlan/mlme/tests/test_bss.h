// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/timekeeper/clock.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include "mock_device.h"

#include <gtest/gtest.h>

namespace wlan {

// TODO(hahnr): Extract into a configuration struct which is passed to frame construction.
// This allows to easily switch between different BSS to join to.
static constexpr uint8_t kBssid1[6] = {0xB7, 0xCD, 0x3F, 0xB0, 0x93, 0x01};
static constexpr uint8_t kBssid2[6] = {0xAC, 0xBF, 0x34, 0x11, 0x95, 0x02};
static constexpr uint32_t kJoinTimeout = 200;       // Beacon Periods
static constexpr uint32_t kAuthTimeout = 200;       // Beacon Periods
static constexpr uint32_t kAutoDeauthTimeout = 100;  // Beacon Periods
static constexpr uint16_t kAid = 2;
static constexpr uint16_t kBeaconPeriodTu = 100;
static constexpr uint16_t kDtimPeriodTu = 2;
static constexpr uint8_t kListenInterval = 10;  // Beacon Periods
static constexpr wlan_channel_t kBssChannel = {
    .cbw = CBW20,
    .primary = 11,
};
static constexpr uint8_t kSsid[] = {'F', 'u', 'c', 'h', 's', 'i', 'a', '-', 'A', 'P'};
static constexpr uint8_t kEapolPdu[] = {'E', 'A', 'P', 'O', 'L'};
static constexpr SupportedRate kSupportedRates[] = {
    SupportedRate(2),  SupportedRate(12), SupportedRate(24), SupportedRate(48),
    SupportedRate(54), SupportedRate(96), SupportedRate(108)};
static constexpr SupportedRate kExtendedSupportedRates[] = {SupportedRate(1), SupportedRate(16),
                                                            SupportedRate(36)};
static constexpr uint8_t kRsne[] = {
    0x30,                    // element id
    0x12,                    // length
    0x00, 0x0f, 0xac, 0x04,  // group data cipher suite
    0x01, 0x00,              // pairwise cipher suite count
    0x00, 0x0f, 0xac, 0x04,  // pairwise cipher suite list
    0x01, 0x00,              // akm suite count
    0x00, 0x0f, 0xac, 0x02,  // akm suite list
    0xa8, 0x04,              // rsn capabilities
};

zx_status_t CreateStartRequest(MlmeMsg<wlan_mlme::StartRequest>*);
zx_status_t CreateJoinRequest(MlmeMsg<wlan_mlme::JoinRequest>*);
zx_status_t CreateAuthRequest(MlmeMsg<wlan_mlme::AuthenticateRequest>*);
zx_status_t CreateAuthResponse(MlmeMsg<wlan_mlme::AuthenticateResponse>*,
                               wlan_mlme::AuthenticateResultCodes result_code);
zx_status_t CreateAssocRequest(MlmeMsg<wlan_mlme::AssociateRequest>* out_msg);
zx_status_t CreateAssocResponse(MlmeMsg<wlan_mlme::AssociateResponse>*,
                                wlan_mlme::AssociateResultCodes result_code);
zx_status_t CreateEapolRequest(MlmeMsg<wlan_mlme::EapolRequest>*);
zx_status_t CreateAuthReqFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateAuthRespFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateBeaconFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateBeaconFrameWithBssid(fbl::unique_ptr<Packet>*, common::MacAddr);
zx_status_t CreateAssocReqFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateAssocRespFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateDataFrame(fbl::unique_ptr<Packet>* out_packet, const uint8_t* payload,
                            size_t len);
zx_status_t CreateNullDataFrame(fbl::unique_ptr<Packet>* out_packet);
zx_status_t CreateEthFrame(fbl::unique_ptr<Packet>* out_packet, const uint8_t* payload, size_t len);

template <typename F> zx_status_t CreateFrame(fbl::unique_ptr<Packet>* pkt) {
    if (std::is_same<F, Authentication>::value) { return CreateAuthRespFrame(pkt); }
    if (std::is_same<F, AssociationResponse>::value) { return CreateAssocRespFrame(pkt); }
    if (std::is_same<F, Beacon>::value) { return CreateBeaconFrame(pkt); }

    return ZX_ERR_NOT_SUPPORTED;
}

template <typename M, typename E>
using enable_if_same = std::enable_if_t<std::is_same<M, E>::value, zx_status_t>;

template <typename M>
enable_if_same<M, wlan_mlme::AuthenticateRequest> CreateMlmeMsg(MlmeMsg<M>* msg) {
    return CreateAuthRequest(msg);
}

template <typename M> enable_if_same<M, wlan_mlme::JoinRequest> CreateMlmeMsg(MlmeMsg<M>* msg) {
    return CreateJoinRequest(msg);
}

template <typename M>
enable_if_same<M, wlan_mlme::AssociateRequest> CreateMlmeMsg(MlmeMsg<M>* msg) {
    return CreateAssocRequest(msg);
}

template <typename T>
static zx_status_t WriteServiceMessage(T* message, uint32_t ordinal, MlmeMsg<T>* out_msg) {
    auto packet = GetSvcPacket(16384);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    zx_status_t status = SerializeServiceMsg(packet.get(), ordinal, message);
    if (status != ZX_OK) { return status; }

    return MlmeMsg<T>::FromPacket(fbl::move(packet), out_msg);
}

}  // namespace wlan
