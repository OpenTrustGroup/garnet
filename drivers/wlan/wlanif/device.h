// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <wlan/protocol/if-impl.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <mutex>

namespace wlanif {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

class Device : public wlan_mlme::MLME {
   public:
    Device(zx_device_t* device, wlanif_impl_protocol_t wlanif_impl_proto);
    ~Device();

    zx_status_t Bind();

    // WLANIF zx_protocol_device_t
    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                      size_t out_len, size_t* out_actual);
    void Unbind();
    void Release();

    // ETHERNET_IMPL zx_protocol_device_t
    void EthUnbind();
    void EthRelease();

    // wlanif_protocol_t (::fuchsia::wlan::mlme -> wlanif-impl)
    void StartScan(wlan_mlme::ScanRequest req) override;
    void JoinReq(wlan_mlme::JoinRequest req) override;
    void AuthenticateReq(wlan_mlme::AuthenticateRequest req) override;
    void AuthenticateResp(wlan_mlme::AuthenticateResponse resp) override;
    void DeauthenticateReq(wlan_mlme::DeauthenticateRequest req) override;
    void AssociateReq(wlan_mlme::AssociateRequest req) override;
    void AssociateResp(wlan_mlme::AssociateResponse resp) override;
    void DisassociateReq(wlan_mlme::DisassociateRequest req) override;
    void ResetReq(wlan_mlme::ResetRequest req) override;
    void StartReq(wlan_mlme::StartRequest req) override;
    void StopReq(wlan_mlme::StopRequest req) override;
    void SetKeysReq(wlan_mlme::SetKeysRequest req) override;
    void DeleteKeysReq(wlan_mlme::DeleteKeysRequest req) override;
    void EapolReq(wlan_mlme::EapolRequest req) override;
    void DeviceQueryReq(wlan_mlme::DeviceQueryRequest req) override;
    void StatsQueryReq() override;
    void MinstrelListReq() override;
    void MinstrelStatsReq(wlan_mlme::MinstrelStatsRequest req) override;

    // wlanif_impl_ifc (wlanif-impl -> ::fuchsia::wlan::mlme)
    void OnScanResult(wlanif_scan_result_t* result);
    void OnScanEnd(wlanif_scan_end_t* result);
    void JoinConf(wlanif_join_confirm_t* resp);
    void AuthenticateConf(wlanif_auth_confirm_t* resp);
    void AuthenticateInd(wlanif_auth_ind_t* ind);
    void DeauthenticateConf(wlanif_deauth_confirm_t* resp);
    void DeauthenticateInd(wlanif_deauth_indication_t* ind);
    void AssociateConf(wlanif_assoc_confirm_t* resp);
    void AssociateInd(wlanif_assoc_ind_t* ind);
    void DisassociateConf(wlanif_disassoc_confirm_t* resp);
    void DisassociateInd(wlanif_disassoc_indication_t* ind);
    void StartConf(wlanif_start_confirm_t* resp);
    void StopConf();
    void EapolConf(wlanif_eapol_confirm_t* resp);
    void SignalReport(wlanif_signal_report_indication_t* ind);
    void EapolInd(wlanif_eapol_indication_t* ind);
    void StatsQueryResp(wlanif_stats_query_response_t* resp);

    // wlanif_protocol_t (ethmac_protocol -> wlanif_impl_protocol)
    zx_status_t EthStart(ethmac_ifc_t* ifc, void* cookie);
    void EthStop();
    zx_status_t EthQuery(uint32_t options, ethmac_info_t* info);
    zx_status_t EthQueueTx(uint32_t options, ethmac_netbuf_t* netbuf);
    zx_status_t EthSetParam(uint32_t param, int32_t value, void* data);

    // wlanif_impl_ifc (wlanif-impl -> ethmac_ifc_t)
    void EthRecv(void* data, size_t length, uint32_t flags);
    void EthCompleteTx(ethmac_netbuf_t* netbuf, zx_status_t status);

   private:
    zx_status_t AddWlanDevice();
    zx_status_t AddEthDevice();

    std::mutex lock_;

    zx_device_t* parent_;
    zx_device_t* zxdev_;   // WLANIF
    zx_device_t* ethdev_;  // ETHERNET_IMPL

    wlanif_impl_protocol_t wlanif_impl_;

    enum data_states {
        OFFLINE,
        ASSOC_REQ_RSN,   // ASSOCIATE.request received, waiting for confirm and RSNA
        ASSOC_REQ_OPEN,  // ASSOCIATE.request received, waiting for confirm
        ONLINE
    } data_state_ __TA_GUARDED(lock_) = OFFLINE;

    void SetDataStateLocked(data_states state) __TA_REQUIRES(lock_);
    void SetDataStateUnlocked(data_states state);

    bool eth_started_ __TA_GUARDED(lock_) = false;
    ethmac_ifc_t ethmac_ifc_ __TA_GUARDED(lock_);
    void* ethmac_cookie_ __TA_GUARDED(lock_);

    bool have_query_info_ __TA_GUARDED(lock_) = false;
    wlanif_query_info query_info_ __TA_GUARDED(lock_);

    async::Loop loop_;
    fidl::Binding<wlan_mlme::MLME> binding_ __TA_GUARDED(lock_);
};

}  // namespace wlanif
