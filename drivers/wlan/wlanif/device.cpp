// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/device.h>
#include <lib/async/cpp/task.h>
#include <net/ethernet.h>
#include <wlan/common/logging.h>
#include <wlan/protocol/ioctl.h>
#include <zircon/status.h>

#include "convert.h"
#include "driver.h"

namespace wlanif {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

Device::Device(zx_device_t* device, wlanif_impl_protocol_t wlanif_impl_proto)
    : parent_(device),
      wlanif_impl_(wlanif_impl_proto),
      loop_(&kAsyncLoopConfigNoAttachToThread),
      binding_(this) {
    debugfn();
}

Device::~Device() {
    debugfn();
}

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t wlanif_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind  = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .ioctl = [](void* ctx, uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                size_t out_len, size_t* out_actual) -> zx_status_t {
        return DEV(ctx)->Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    },
};

static zx_protocol_device_t eth_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->EthUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->EthRelease(); },
};

static wlanif_impl_ifc_t wlanif_impl_ifc_ops = {
    // MLME operations
    .on_scan_result = [](void* cookie, wlanif_scan_result_t* result)
                          { DEV(cookie)->OnScanResult(result); },
    .on_scan_end = [](void* cookie, wlanif_scan_end_t* end)
                       { DEV(cookie)->OnScanEnd(end); },
    .join_conf = [](void* cookie, wlanif_join_confirm_t* resp)
                     { DEV(cookie)->JoinConf(resp); },
    .auth_conf = [](void* cookie, wlanif_auth_confirm_t* resp)
                     { DEV(cookie)->AuthenticateConf(resp); },
    .auth_ind = [](void* cookie, wlanif_auth_ind_t* ind)
                     { DEV(cookie)->AuthenticateInd(ind); },
    .deauth_conf = [](void* cookie, wlanif_deauth_confirm_t* resp)
                       { DEV(cookie)->DeauthenticateConf(resp); },
    .deauth_ind = [](void* cookie, wlanif_deauth_indication_t* ind)
                      { DEV(cookie)->DeauthenticateInd(ind); },
    .assoc_conf = [](void* cookie, wlanif_assoc_confirm_t* resp)
                      { DEV(cookie)->AssociateConf(resp); },
    .assoc_ind = [](void* cookie, wlanif_assoc_ind_t* ind)
                      { DEV(cookie)->AssociateInd(ind); },
    .disassoc_conf = [](void* cookie, wlanif_disassoc_confirm_t* resp)
                         { DEV(cookie)->DisassociateConf(resp); },
    .disassoc_ind = [](void* cookie, wlanif_disassoc_indication_t* ind)
                        { DEV(cookie)->DisassociateInd(ind); },
    .start_conf = [](void* cookie, wlanif_start_confirm_t* resp)
                      { DEV(cookie)->StartConf(resp); },
    .stop_conf = [](void* cookie)
                     { DEV(cookie)->StopConf(); },
    .eapol_conf = [](void* cookie, wlanif_eapol_confirm_t* resp)
                      { DEV(cookie)->EapolConf(resp); },

    // MLME extension operations
    .signal_report = [](void* cookie, wlanif_signal_report_indication_t* ind)
                         { DEV(cookie)->SignalReport(ind); },
    .eapol_ind = [](void* cookie, wlanif_eapol_indication_t* ind)
                     { DEV(cookie)->EapolInd(ind); },
    .stats_query_resp = [](void* cookie, wlanif_stats_query_response_t* resp)
                            { DEV(cookie)->StatsQueryResp(resp); },

    // Ethernet operations
    .data_recv = [](void* cookie, void* data, size_t length, uint32_t flags)
                     { DEV(cookie)->EthRecv(data, length, flags); },
    .data_complete_tx = [](void* cookie, ethmac_netbuf_t* netbuf, zx_status_t status)
                            { DEV(cookie)->EthCompleteTx(netbuf, status); },
};

static ethmac_protocol_ops_t ethmac_ops = {
    .query = [](void* ctx, uint32_t options, ethmac_info_t* info) -> zx_status_t
                 { return DEV(ctx)->EthQuery(options, info); },
    .stop = [](void* ctx)
                { DEV(ctx)->EthStop(); },
    .start = [](void* ctx, ethmac_ifc_t* ifc, void* cookie) -> zx_status_t
                 { return DEV(ctx)->EthStart(ifc, cookie); },
    .queue_tx = [](void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) -> zx_status_t
                    { return DEV(ctx)->EthQueueTx(options, netbuf); },
    .set_param = [](void* ctx, uint32_t param, int32_t value, void* data) -> zx_status_t
                     { return DEV(ctx)->EthSetParam(param, value, data); },
};
#undef DEV

zx_status_t Device::AddWlanDevice() {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlanif";
    args.ctx = this;
    args.ops = &wlanif_device_ops;
    args.proto_id = ZX_PROTOCOL_WLANIF;
    return device_add(parent_, &args, &zxdev_);
}

zx_status_t Device::AddEthDevice() {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlan-ethernet";
    args.ctx = this;
    args.ops = &eth_device_ops;
    args.proto_id = ZX_PROTOCOL_ETHERNET_IMPL;
    args.proto_ops = &ethmac_ops;
    return device_add(zxdev_, &args, &ethdev_);
}

#define VERIFY_PROTO_OP(fn)                                                  \
    do {                                                                     \
        if (wlanif_impl_.ops->fn == nullptr) {                               \
            errorf("wlanif: required protocol function %s missing\n", #fn);  \
            return ZX_ERR_INVALID_ARGS;                                      \
        }                                                                    \
    } while (0)

zx_status_t Device::Bind() {
    debugfn();

    // Assert minimum required functionality from the wlanif_impl driver
    if (wlanif_impl_.ops == nullptr) {
        errorf("wlanif: no wlanif_impl protocol ops provided\n");
        return ZX_ERR_INVALID_ARGS;
    }
    VERIFY_PROTO_OP(start);
    VERIFY_PROTO_OP(query);
    VERIFY_PROTO_OP(start_scan);
    VERIFY_PROTO_OP(join_req);
    VERIFY_PROTO_OP(auth_req);
    VERIFY_PROTO_OP(auth_resp);
    VERIFY_PROTO_OP(deauth_req);
    VERIFY_PROTO_OP(assoc_req);
    VERIFY_PROTO_OP(assoc_resp);
    VERIFY_PROTO_OP(disassoc_req);
    VERIFY_PROTO_OP(reset_req);
    VERIFY_PROTO_OP(start_req);
    VERIFY_PROTO_OP(stop_req);
    VERIFY_PROTO_OP(set_keys_req);
    VERIFY_PROTO_OP(del_keys_req);
    VERIFY_PROTO_OP(eapol_req);

    // The MLME interface has no start/stop commands, so we will start the wlanif_impl
    // device immediately
    zx_status_t status = wlanif_impl_.ops->start(wlanif_impl_.ctx, &wlanif_impl_ifc_ops, this);
    if (status != ZX_OK) {
        errorf("wlanif: call to wlanif-impl start() failed: %s\n", zx_status_get_string(status));
        return status;
    }

    status = loop_.StartThread("wlanif-loop");
    if (status != ZX_OK) {
        errorf("wlanif: unable to start async loop: %s\n", zx_status_get_string(status));
        return status;
    }

    status = AddWlanDevice();
    if (status != ZX_OK) {
        errorf("wlanif: could not add wlanif device: %s\n", zx_status_get_string(status));
        loop_.Shutdown();
        return status;
    }

    status = AddEthDevice();
    if (status != ZX_OK) {
        errorf("wlanif: could not add ethernet_impl device: %s\n", zx_status_get_string(status));
        loop_.Shutdown();
    }

    return status;
}
#undef VERIFY_PROTO_OP

zx_status_t Device::Ioctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len, size_t* out_actual) {
    debugfn();
    switch (op) {
    case IOCTL_WLAN_GET_CHANNEL:
        {
            if (out_buf == nullptr || out_len < sizeof(zx_handle_t)) {
                return ZX_ERR_INVALID_ARGS;
            }
            std::lock_guard<std::mutex> lock(lock_);
            if (binding_.is_bound()) {
                return ZX_ERR_ALREADY_BOUND;
            }
            fidl::InterfaceHandle<wlan_mlme::MLME> ifc_handle;
            ifc_handle = binding_.NewBinding(loop_.dispatcher());
            if (!ifc_handle.is_valid()) {
                errorf("wlanif: unable to create new channel\n");
                return ZX_ERR_NO_RESOURCES;
            }
            zx::channel out_ch = ifc_handle.TakeChannel();
            zx_handle_t* out_handle = static_cast<zx_handle_t*>(out_buf);
            *out_handle = out_ch.release();
            *out_actual = sizeof(zx_handle_t);
            return ZX_OK;
        }
    default:
        errorf("ioctl unknown: %0x\n", op);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void Device::EthUnbind() {
    debugfn();
    device_remove(ethdev_);
}

void Device::EthRelease() {
    debugfn();
    // NOTE: we reuse the same ctx for the wlanif and the ethmac, so we do NOT free the memory here.
    // Since ethdev_ is a child of zxdev_, this release will be called first, followed by
    // Release. There's nothing else to clean up here.
}

void Device::Unbind() {
    debugfn();

    // Stop accepting new FIDL requests.
    std::lock_guard<std::mutex> lock(lock_);
    if (binding_.is_bound()) {
        binding_.Unbind();
    }

    // Ensure that all FIDL messages have been processed before removing the device
    auto dispatcher = loop_.dispatcher();
    ::async::PostTask(dispatcher, [this] { device_remove(zxdev_); });
}

void Device::Release() {
    debugfn();
    delete this;
}

void Device::StartScan(wlan_mlme::ScanRequest req) {
    wlanif_scan_req_t impl_req = {};

    // txn_id
    impl_req.txn_id = req.txn_id;

    // bss_type
    impl_req.bss_type = ConvertBSSType(req.bss_type);

    // bssid
    std::memcpy(impl_req.bssid, req.bssid.data(), ETH_ALEN);

    // ssid
    CopySSID(req.ssid, &impl_req.ssid);

    // scan_type
    impl_req.scan_type = ConvertScanType(req.scan_type);

    // probe_delay
    impl_req.probe_delay = req.probe_delay;

    // channel_list
    if (req.channel_list->size() > WLAN_CHANNELS_MAX_LEN) {
        warnf("wlanif: truncating channel list from %lu to %d\n", req.channel_list->size(),
              WLAN_CHANNELS_MAX_LEN);
        impl_req.num_channels = WLAN_CHANNELS_MAX_LEN;
    } else {
        impl_req.num_channels = req.channel_list->size();
    }
    std::memcpy(impl_req.channel_list, req.channel_list->data(), impl_req.num_channels);

    // min_channel_time
    impl_req.min_channel_time = req.min_channel_time;

    // max_channel_time
    impl_req.max_channel_time = req.max_channel_time;

    // ssid_list
    size_t num_ssids = req.ssid_list->size();
    if (num_ssids > WLAN_SCAN_MAX_SSIDS) {
        warnf("wlanif: truncating SSID list from %zu to %d\n", num_ssids, WLAN_SCAN_MAX_SSIDS);
        num_ssids = WLAN_SCAN_MAX_SSIDS;
    }
    for (size_t ndx = 0; ndx < num_ssids; ndx++) {
        CopySSID((*req.ssid_list)[ndx], &impl_req.ssid_list[ndx]);
    }
    impl_req.num_ssids = num_ssids;

    wlanif_impl_.ops->start_scan(wlanif_impl_.ctx, &impl_req);
}

void Device::JoinReq(wlan_mlme::JoinRequest req) {
    SetDataStateUnlocked(OFFLINE);

    wlanif_join_req_t impl_req = {};

    // selected_bss
    ConvertBSSDescription(&impl_req.selected_bss, &req.selected_bss);

    // join_failure_timeout
    impl_req.join_failure_timeout = req.join_failure_timeout;

    // nav_sync_delay
    impl_req.nav_sync_delay = req.nav_sync_delay;

    // op_rates
    if (req.op_rate_set->size() > WLAN_MAX_OP_RATES) {
        warnf("wlanif: truncating operational rates set from %zu to %d members\n",
              req.op_rate_set->size(), WLAN_MAX_OP_RATES);
        impl_req.num_op_rates = WLAN_MAX_OP_RATES;
    } else {
        impl_req.num_op_rates = req.op_rate_set->size();
    }
    std::memcpy(impl_req.op_rates, req.op_rate_set->data(), impl_req.num_op_rates);

    wlanif_impl_.ops->join_req(wlanif_impl_.ctx, &impl_req);
}

void Device::AuthenticateReq(wlan_mlme::AuthenticateRequest req) {
    SetDataStateUnlocked(OFFLINE);

    wlanif_auth_req_t impl_req = {};

    // peer_sta_address
    std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

    // auth_type
    impl_req.auth_type = ConvertAuthType(req.auth_type);

    // auth_failure_timeout
    impl_req.auth_failure_timeout = req.auth_failure_timeout;

    wlanif_impl_.ops->auth_req(wlanif_impl_.ctx, &impl_req);
}

void Device::AuthenticateResp(wlan_mlme::AuthenticateResponse resp) {
    wlanif_auth_resp_t impl_resp = {};

    // peer_sta_address
    std::memcpy(impl_resp.peer_sta_address, resp.peer_sta_address.data(), ETH_ALEN);

    // result_code
    impl_resp.result_code = ConvertAuthResultCode(resp.result_code);

    wlanif_impl_.ops->auth_resp(wlanif_impl_.ctx, &impl_resp);
}

void Device::DeauthenticateReq(wlan_mlme::DeauthenticateRequest req) {
    SetDataStateUnlocked(OFFLINE);

    wlanif_deauth_req_t impl_req = {};

    // peer_sta_address
    std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

    // reason_code
    impl_req.reason_code = ConvertDeauthReasonCode(req.reason_code);

    wlanif_impl_.ops->deauth_req(wlanif_impl_.ctx, &impl_req);
}

void Device::AssociateReq(wlan_mlme::AssociateRequest req) {
    bool is_protected = !req.rsn.is_null();
    SetDataStateUnlocked(is_protected ? ASSOC_REQ_RSN : ASSOC_REQ_OPEN);

    wlanif_assoc_req_t impl_req = {};

    // peer_sta_address
    std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

    // rsne
    if (is_protected) {
        CopyRSNE(req.rsn, impl_req.rsne, &impl_req.rsne_len);
    } else {
        impl_req.rsne_len = 0;
    }

    wlanif_impl_.ops->assoc_req(wlanif_impl_.ctx, &impl_req);
}

void Device::AssociateResp(wlan_mlme::AssociateResponse resp) {
    wlanif_assoc_resp_t impl_resp = {};

    // peer_sta_address
    std::memcpy(impl_resp.peer_sta_address, resp.peer_sta_address.data(), ETH_ALEN);

    // result_code
    impl_resp.result_code = ConvertAssocResultCode(resp.result_code);

    // association_id
    impl_resp.association_id = resp.association_id;

    wlanif_impl_.ops->assoc_resp(wlanif_impl_.ctx, &impl_resp);
}

void Device::DisassociateReq(wlan_mlme::DisassociateRequest req) {
    SetDataStateUnlocked(OFFLINE);

    wlanif_disassoc_req_t impl_req = {};

    // peer_sta_address
    std::memcpy(impl_req.peer_sta_address, req.peer_sta_address.data(), ETH_ALEN);

    // reason_code
    impl_req.reason_code = req.reason_code;

    wlanif_impl_.ops->disassoc_req(wlanif_impl_.ctx, &impl_req);
}

void Device::ResetReq(wlan_mlme::ResetRequest req) {
    SetDataStateUnlocked(OFFLINE);

    wlanif_reset_req_t impl_req = {};

    // sta_address
    std::memcpy(impl_req.sta_address, req.sta_address.data(), ETH_ALEN);

    // set_default_mib
    impl_req.set_default_mib = req.set_default_mib;

    wlanif_impl_.ops->reset_req(wlanif_impl_.ctx, &impl_req);
}

void Device::StartReq(wlan_mlme::StartRequest req) {
    SetDataStateUnlocked(ONLINE);

    wlanif_start_req_t impl_req = {};

    // ssid
    CopySSID(req.ssid, &impl_req.ssid);

    // bss_type
    impl_req.bss_type = ConvertBSSType(req.bss_type);

    // beacon_period
    impl_req.beacon_period = req.beacon_period;

    // dtim_period
    impl_req.dtim_period = req.dtim_period;

    // channel
    impl_req.channel = req.channel;

    // rsne
    CopyRSNE(req.rsne, impl_req.rsne, &impl_req.rsne_len);

    wlanif_impl_.ops->start_req(wlanif_impl_.ctx, &impl_req);
}

void Device::StopReq(wlan_mlme::StopRequest req) {
    SetDataStateUnlocked(OFFLINE);

    wlanif_stop_req_t impl_req = {};

    // ssid
    CopySSID(req.ssid, &impl_req.ssid);

    wlanif_impl_.ops->stop_req(wlanif_impl_.ctx, &impl_req);
}

void Device::SetKeysReq(wlan_mlme::SetKeysRequest req) {
    wlanif_set_keys_req_t impl_req = {};

    // keylist
    size_t num_keys = req.keylist->size();
    if (num_keys > WLAN_MAX_KEYLIST_SIZE) {
        warnf("wlanif: truncating key list from %zu to %d members\n", num_keys,
              WLAN_MAX_KEYLIST_SIZE);
        impl_req.num_keys = WLAN_MAX_KEYLIST_SIZE;
    } else {
        impl_req.num_keys = num_keys;
    }
    for (size_t desc_ndx = 0; desc_ndx < num_keys; desc_ndx++) {
        ConvertSetKeyDescriptor(&impl_req.keylist[desc_ndx], &(*req.keylist)[desc_ndx]);
    }

    // TODO: Track keys more accurately (NET-1439)
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (data_state_ == ASSOC_REQ_RSN) {
            SetDataStateLocked(ONLINE);
        }
    }

    wlanif_impl_.ops->set_keys_req(wlanif_impl_.ctx, &impl_req);
}

void Device::DeleteKeysReq(wlan_mlme::DeleteKeysRequest req) {
    wlanif_del_keys_req_t impl_req = {};

    // keylist
    size_t num_keys = req.keylist->size();
    if (num_keys > WLAN_MAX_KEYLIST_SIZE) {
        warnf("wlanif: truncating key list from %zu to %d members\n", num_keys,
              WLAN_MAX_KEYLIST_SIZE);
        impl_req.num_keys = WLAN_MAX_KEYLIST_SIZE;
    } else {
        impl_req.num_keys = num_keys;
    }
    for (size_t desc_ndx = 0; desc_ndx < num_keys; desc_ndx++) {
        ConvertDeleteKeyDescriptor(&impl_req.keylist[desc_ndx], &(*req.keylist)[desc_ndx]);
    }

    // TODO: Track keys more accurately (NET-1439)
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (data_state_ == ONLINE) {
            SetDataStateLocked(ASSOC_REQ_RSN);
        }
    }

    wlanif_impl_.ops->del_keys_req(wlanif_impl_.ctx, &impl_req);
}

void Device::EapolReq(wlan_mlme::EapolRequest req) {
    wlanif_eapol_req_t impl_req = {};

    // src_addr
    std::memcpy(impl_req.src_addr, req.src_addr.data(), ETH_ALEN);

    // dst_addr
    std::memcpy(impl_req.dst_addr, req.dst_addr.data(), ETH_ALEN);

    // data
    impl_req.data_len = req.data->size();
    impl_req.data = req.data->data();

    wlanif_impl_.ops->eapol_req(wlanif_impl_.ctx, &impl_req);
}

void Device::DeviceQueryReq(wlan_mlme::DeviceQueryRequest req) {
    std::lock_guard<std::mutex> lock(lock_);

    if (!have_query_info_) {
        wlanif_impl_.ops->query(wlanif_impl_.ctx, &query_info_);
        have_query_info_ = true;
    }

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::DeviceQueryConfirm fidl_resp;

    // mac_addr
    std::memcpy(fidl_resp.mac_addr.mutable_data(), query_info_.mac_addr, ETH_ALEN);

    // role
    fidl_resp.role = ConvertMacRole(query_info_.role);

    // bands
    fidl_resp.bands.resize(query_info_.num_bands);
    for (size_t ndx = 0; ndx < query_info_.num_bands; ndx++) {
        ConvertBandCapabilities(&(*fidl_resp.bands)[ndx], &query_info_.bands[ndx]);
    }

    binding_.events().DeviceQueryConf(std::move(fidl_resp));
}

void Device::StatsQueryReq() {
    if (wlanif_impl_.ops->stats_query_req != nullptr) {
        wlanif_impl_.ops->stats_query_req(wlanif_impl_.ctx);
    }
}

void Device::MinstrelListReq() {
    errorf("Minstrel peer list not available: FullMAC driver not supported.\n");
    ZX_DEBUG_ASSERT(false);

    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) { return; }

    binding_.events().MinstrelListResp(wlan_mlme::MinstrelListResponse{});
}

void Device::MinstrelStatsReq(wlan_mlme::MinstrelStatsRequest req) {
    errorf("Minstrel stats not available: FullMAC driver not supported.\n");
    ZX_DEBUG_ASSERT(false);

    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) { return; }

    binding_.events().MinstrelStatsResp(wlan_mlme::MinstrelStatsResponse{});
}

void Device::OnScanResult(wlanif_scan_result_t* result) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::ScanResult fidl_result;

    // txn_id
    fidl_result.txn_id = result->txn_id;

    // bss
    ConvertBSSDescription(&fidl_result.bss, &result->bss);

    binding_.events().OnScanResult(std::move(fidl_result));
}

void Device::OnScanEnd(wlanif_scan_end_t* end) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::ScanEnd fidl_end;

    // txn_id
    fidl_end.txn_id = end->txn_id;

    // code
    fidl_end.code = ConvertScanResultCode(end->code);

    binding_.events().OnScanEnd(std::move(fidl_end));
}

void Device::JoinConf(wlanif_join_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::JoinConfirm fidl_resp;

    // result_code
    fidl_resp.result_code = ConvertJoinResultCode(resp->result_code);

    binding_.events().JoinConf(std::move(fidl_resp));
}

void Device::AuthenticateConf(wlanif_auth_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::AuthenticateConfirm fidl_resp;

    // peer_sta_address
    std::memcpy(fidl_resp.peer_sta_address.mutable_data(), resp->peer_sta_address, ETH_ALEN);

    // auth_type
    fidl_resp.auth_type = ConvertAuthType(resp->auth_type);

    // result_code
    fidl_resp.result_code = ConvertAuthResultCode(resp->result_code);

    binding_.events().AuthenticateConf(std::move(fidl_resp));
}

void Device::AuthenticateInd(wlanif_auth_ind_t* ind) {
    std::lock_guard<std::mutex> lock(lock_);

    if (!binding_.is_bound()) { return; }

    wlan_mlme::AuthenticateIndication fidl_ind;

    // peer_sta_address
    std::memcpy(fidl_ind.peer_sta_address.mutable_data(), ind->peer_sta_address, ETH_ALEN);

    // auth_type
    fidl_ind.auth_type = ConvertAuthType(ind->auth_type);

    binding_.events().AuthenticateInd(std::move(fidl_ind));
}

void Device::DeauthenticateConf(wlanif_deauth_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::DeauthenticateConfirm fidl_resp;

    // peer_sta_address
    std::memcpy(fidl_resp.peer_sta_address.mutable_data(), resp->peer_sta_address, ETH_ALEN);

    binding_.events().DeauthenticateConf(std::move(fidl_resp));
}

void Device::DeauthenticateInd(wlanif_deauth_indication_t* ind) {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::DeauthenticateIndication fidl_ind;

    // peer_sta_address
    std::memcpy(fidl_ind.peer_sta_address.mutable_data(), ind->peer_sta_address, ETH_ALEN);

    // reason_code
    fidl_ind.reason_code = ConvertDeauthReasonCode(ind->reason_code);

    binding_.events().DeauthenticateInd(std::move(fidl_ind));
}

void Device::AssociateConf(wlanif_assoc_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);

    if (resp->result_code == WLAN_ASSOC_RESULT_SUCCESS && data_state_ == ASSOC_REQ_OPEN) {
        SetDataStateLocked(ONLINE);
    }

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::AssociateConfirm fidl_resp;

    // result_code
    fidl_resp.result_code = ConvertAssocResultCode(resp->result_code);

    // association_id
    fidl_resp.association_id = resp->association_id;

    binding_.events().AssociateConf(std::move(fidl_resp));
}

void Device::AssociateInd(wlanif_assoc_ind_t* ind) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::AssociateIndication fidl_ind;

    // peer_sta_address
    std::memcpy(fidl_ind.peer_sta_address.mutable_data(), ind->peer_sta_address, ETH_ALEN);

    // listen_interval
    fidl_ind.listen_interval = ind->listen_interval;

    // TODO(NET-1460): SSID in wlanif_assoc_ind_t is a char[] but should be a uint8_t[] as the
    // SSID is not a string. Another problem is that wlanif_assoc_ind_t doesn't carry the
    // SSID's length and thus we cannot determine if the SSID is optional or not.

    // rsne
    bool is_protected = ind->rsne_len != 0;
    if (is_protected) {
        memcpy(fidl_ind.rsn->data(), ind->rsne, ind->rsne_len);
    }

    binding_.events().AssociateInd(std::move(fidl_ind));
}

void Device::DisassociateConf(wlanif_disassoc_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::DisassociateConfirm fidl_resp;

    // status
    fidl_resp.status = resp->status;

    binding_.events().DisassociateConf(std::move(fidl_resp));
}

void Device::DisassociateInd(wlanif_disassoc_indication_t* ind) {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::DisassociateIndication fidl_ind;

    // peer_sta_address
    std::memcpy(fidl_ind.peer_sta_address.mutable_data(), ind->peer_sta_address, ETH_ALEN);

    // reason_code
    fidl_ind.reason_code = ind->reason_code;

    binding_.events().DisassociateInd(std::move(fidl_ind));
}

void Device::StartConf(wlanif_start_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);

    if (resp->result_code == WLAN_START_RESULT_SUCCESS) {
        SetDataStateLocked(ONLINE);
    }

    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::StartConfirm fidl_resp;

    // result_code
    fidl_resp.result_code = ConvertStartResultCode(resp->result_code);

    binding_.events().StartConf(std::move(fidl_resp));
}

void Device::StopConf() {
    std::lock_guard<std::mutex> lock(lock_);

    SetDataStateLocked(OFFLINE);

    if (!binding_.is_bound()) {
        return;
    }

    binding_.events().StopConf();
}

void Device::EapolConf(wlanif_eapol_confirm_t* resp) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::EapolConfirm fidl_resp;

    // result_code
    fidl_resp.result_code = ConvertEapolResultCode(resp->result_code);

    binding_.events().EapolConf(std::move(fidl_resp));
}

void Device::SignalReport(wlanif_signal_report_indication_t* ind) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::SignalReportIndication fidl_ind;

    // rssi_dbm
    fidl_ind.rssi_dbm = ind->rssi_dbm;

    binding_.events().SignalReport(std::move(fidl_ind));
}

void Device::EapolInd(wlanif_eapol_indication_t* ind) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!binding_.is_bound()) {
        return;
    }

    wlan_mlme::EapolIndication fidl_ind;

    // src_addr
    std::memcpy(fidl_ind.src_addr.mutable_data(), ind->src_addr, ETH_ALEN);

    // dst_addr
    std::memcpy(fidl_ind.dst_addr.mutable_data(), ind->dst_addr, ETH_ALEN);

    // data
    fidl_ind.data->resize(ind->data_len);
    fidl_ind.data->assign(ind->data, ind->data + ind->data_len);

    binding_.events().EapolInd(std::move(fidl_ind));
}

void Device::StatsQueryResp(wlanif_stats_query_response_t* resp) {
    // TODO: NET-1376
    errorf("wlanif: stats_query_resp message not yet supported (see NET-1376)\n");
}

zx_status_t Device::EthStart(ethmac_ifc_t* ifc, void* cookie) {
    std::lock_guard<std::mutex> lock(lock_);
    ethmac_ifc_ = *ifc;
    ethmac_cookie_ = cookie;
    eth_started_ = true;
    return ZX_OK;
}

void Device::EthStop() {
    std::lock_guard<std::mutex> lock(lock_);
    eth_started_ = false;
    ethmac_cookie_ = nullptr;
    std::memset(&ethmac_ifc_, 0, sizeof(ethmac_ifc_));
}

zx_status_t Device::EthQuery(uint32_t options, ethmac_info_t* info) {
    std::lock_guard<std::mutex> lock(lock_);

    if (!have_query_info_) {
        wlanif_impl_.ops->query(wlanif_impl_.ctx, &query_info_);
        have_query_info_ = true;
    }

    std::memset(info, 0, sizeof(*info));

    // features
    info->features = ETHMAC_FEATURE_WLAN;
    if (query_info_.features & WLANIF_FEATURE_DMA) { info->features |= ETHMAC_FEATURE_DMA; }
    if (query_info_.features & WLANIF_FEATURE_SYNTH) { info->features |= ETHMAC_FEATURE_SYNTH; }

    // mtu
    info->mtu = 1500;

    // mac
    std::memcpy(info->mac, query_info_.mac_addr, ETH_ALEN);

    return ZX_OK;
}

zx_status_t Device::EthQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
    if (wlanif_impl_.ops->data_queue_tx != nullptr) {
        return wlanif_impl_.ops->data_queue_tx(wlanif_impl_.ctx, options, netbuf);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t Device::EthSetParam(uint32_t param, int32_t value, void* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Device::SetDataStateLocked(data_states new_state) {
    if (((new_state == ONLINE) != (data_state_ == ONLINE)) && eth_started_) {
        ethmac_ifc_.status(ethmac_cookie_, new_state == ONLINE ? ETH_STATUS_ONLINE : 0);
    }
    data_state_ = new_state;
}

void Device::SetDataStateUnlocked(data_states new_state) {
    std::lock_guard<std::mutex> lock(lock_);
    SetDataStateLocked(new_state);
}

void Device::EthRecv(void* data, size_t length, uint32_t flags) {
    std::lock_guard<std::mutex> lock(lock_);
    if (eth_started_) {
        ethmac_ifc_.recv(ethmac_cookie_, data, length, flags);
    }
}

void Device::EthCompleteTx(ethmac_netbuf_t* netbuf, zx_status_t status) {
    std::lock_guard<std::mutex> lock(lock_);
    if (eth_started_) {
        ethmac_ifc_.complete_tx(ethmac_cookie_, netbuf, status);
    }
}

}  // namespace wlanif
