// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/scanner.h>

#include <wlan/common/logging.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/sequence.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/wlan.h>

#include "lib/fidl/cpp/vector.h"

#include <fbl/unique_ptr.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <cinttypes>
#include <utility>

namespace wlan {

Scanner::Scanner(DeviceInterface* device, fbl::unique_ptr<Timer> timer)
    : device_(device), timer_(std::move(timer)) {
    ZX_DEBUG_ASSERT(timer_.get());
}

zx_status_t Scanner::HandleMlmeScanReq(const wlan_mlme::ScanRequest& req) {
    return Start(req);
}

zx_status_t Scanner::Start(const wlan_mlme::ScanRequest& req) {
    debugfn();

    if (IsRunning()) { return ZX_ERR_UNAVAILABLE; }
    ZX_DEBUG_ASSERT(channel_index_ == 0);
    ZX_DEBUG_ASSERT(channel_start_.get() == 0);

    resp_ = wlan_mlme::ScanConfirm::New();
    resp_->bss_description_set = fidl::VectorPtr<wlan_mlme::BSSDescription>::New(0);
    resp_->result_code = wlan_mlme::ScanResultCodes::NOT_SUPPORTED;

    if (req.channel_list->size() == 0) { return SendScanConfirm(); }
    if (req.max_channel_time < req.min_channel_time) { return SendScanConfirm(); }
    // TODO(NET-629): re-enable checking the enum value after fidl2 lands
    //if (!BSSTypes_IsValidValue(req.bss_type) || !ScanTypes_IsValidValue(req.scan_type)) {
    //    return SendScanConfirm();
    //}

    // TODO(tkilbourn): define another result code (out of spec) for errors that aren't
    // NOT_SUPPORTED errors. Then set SUCCESS only when we've successfully finished scanning.
    resp_->result_code = wlan_mlme::ScanResultCodes::SUCCESS;
    req_ = wlan_mlme::ScanRequest::New();
    zx_status_t status = req.Clone(req_.get());
    if (status != ZX_OK) {
        errorf("could not clone Scanrequest: %d\n", status);
        Reset();
        return status;
    }

    channel_start_ = timer_->Now();
    zx::time timeout = InitialTimeout();
    status = device_->SetChannel(ScanChannel());
    if (status != ZX_OK) {
        errorf("could not queue set channel: %d\n", status);
        SendScanConfirm();
        Reset();
        return status;
    }

    status = timer_->SetTimer(timeout);
    if (status != ZX_OK) {
        errorf("could not start scan timer: %d\n", status);
        resp_->result_code = wlan_mlme::ScanResultCodes::NOT_SUPPORTED;
        SendScanConfirm();
        Reset();
        return status;
    }

    return ZX_OK;
}

void Scanner::Reset() {
    debugfn();
    req_.reset();
    resp_.reset();
    channel_index_ = 0;
    channel_start_ = zx::time();
    timer_->CancelTimer();
    nbrs_bss_.Clear();
}

bool Scanner::IsRunning() const {
    return req_ != nullptr;
}

Scanner::Type Scanner::ScanType() const {
    ZX_DEBUG_ASSERT(IsRunning());
    switch (req_->scan_type) {
    case wlan_mlme::ScanTypes::PASSIVE:
        return Type::kPassive;
    case wlan_mlme::ScanTypes::ACTIVE:
        return Type::kActive;
    }
}

wlan_channel_t Scanner::ScanChannel() const {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());
    ZX_DEBUG_ASSERT(channel_index_ < req_->channel_list->size());
    return wlan_channel_t{
        .primary = req_->channel_list->at(channel_index_),
    };
}

zx_status_t Scanner::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    // Ignore all management frames when scanner is not running.
    return IsRunning() ? ZX_OK : ZX_ERR_STOP;
}

zx_status_t Scanner::HandleBeacon(const ImmutableMgmtFrame<Beacon>& frame,
                                  const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());

    // Before processing Beacon, remove stale entries.
    RemoveStaleBss();

    auto hdr = frame.hdr;
    common::MacAddr bssid(hdr->addr3);
    common::MacAddr src_addr(hdr->addr2);

    if (bssid != src_addr) {
        // Undefined situation. Investigate if roaming needs this or this is a plain dark art.
        debugbcn("Rxed a beacon/probe_resp from the non-BSSID station: BSSID %s   SrcAddr %s\n",
                 MACSTR(bssid), MACSTR(src_addr));
        return ZX_OK;  // Do not process.
    }

    // Update existing BSS or insert if already in map.
    zx_status_t status = ZX_OK;
    auto bss = nbrs_bss_.Lookup(bssid);
    if (bss != nullptr) {
        status = bss->ProcessBeacon(frame.body, frame.body_len, &rxinfo);
    } else if (nbrs_bss_.IsFull()) {
        errorf("error, maximum number of BSS reached: %lu\n", nbrs_bss_.Count());
    } else {
        bss = fbl::AdoptRef(new Bss(bssid));
        bss->ProcessBeacon(frame.body, frame.body_len, &rxinfo);
        status = nbrs_bss_.Insert(bssid, bss);
    }

    if (status != ZX_OK) {
        debugbcn("Failed to handle beacon (err %3d): BSSID %s timestamp: %15" PRIu64 "\n", status,
                 MACSTR(bssid), frame.body->timestamp);
    }

    return ZX_OK;
}

void Scanner::RemoveStaleBss() {
    // TODO(porce): call this periodically and delete stale entries.
    // TODO(porce): Implement a complex preemption logic here.

    // Only prune if necessary time passed.
    static zx::time ts_last_prune;
    auto now = zx::clock::get(ZX_CLOCK_UTC);
    if (ts_last_prune + kBssPruneDelay > now) { return; }

    // Prune stale entries.
    ts_last_prune = now;
    nbrs_bss_.RemoveIf(
        [now](fbl::RefPtr<Bss> bss) -> bool { return (bss->ts_refreshed() + kBssExpiry >= now); });
}

zx_status_t Scanner::HandleProbeResponse(const ImmutableMgmtFrame<ProbeResponse>& frame,
                                         const wlan_rx_info_t& rxinfo) {
    debugfn();

    // A ProbeResponse carries all currently used attributes of a Beacon frame. Hence, treat a
    // ProbeResponse as a Beacon for now to support active scanning. There are additional
    // information for either frame type which we have to process on a per frame type basis in the
    // future. For now, stick with this kind of unification.
    // TODO(hahnr): The should probably moved somehow into the Dispatcher.
    auto bcn = reinterpret_cast<const Beacon*>(frame.body);
    auto mgmt_frame = ImmutableMgmtFrame<Beacon>(frame.hdr, bcn, frame.body_len);

    HandleBeacon(mgmt_frame, rxinfo);
    return ZX_OK;
}

zx_status_t Scanner::HandleTimeout() {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());

    zx::time now = timer_->Now();
    zx_status_t status = ZX_OK;

    // Reached max channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->max_channel_time)) {
        if (++channel_index_ >= req_->channel_list->size()) {
            timer_->CancelTimer();
            status = SendScanConfirm();
            Reset();
            return status;
        } else {
            channel_start_ = timer_->Now();
            status = timer_->SetTimer(InitialTimeout());
            if (status != ZX_OK) { goto timer_fail; }
            return device_->SetChannel(ScanChannel());
        }
    }

    // TODO(tkilbourn): can probe delay come after min_channel_time?

    // Reached min channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->min_channel_time)) {
        // TODO(tkilbourn): if there was no sign of activity on this channel, skip ahead to the next
        // one
        // For now, just continue the scan.
        zx::time timeout = channel_start_ + WLAN_TU(req_->max_channel_time);
        status = timer_->SetTimer(timeout);
        if (status != ZX_OK) { goto timer_fail; }
        return ZX_OK;
    }

    // Reached probe delay for an active scan
    if (req_->scan_type == wlan_mlme::ScanTypes::ACTIVE &&
        now >= channel_start_ + WLAN_TU(req_->probe_delay)) {
        debugf("Reached probe delay\n");
        // TODO(hahnr): Add support for CCA as described in IEEE Std 802.11-2016 11.1.4.3.2 f)
        zx::time timeout = channel_start_ + WLAN_TU(req_->min_channel_time);
        status = timer_->SetTimer(timeout);
        if (status != ZX_OK) { goto timer_fail; }
        SendProbeRequest();
        return ZX_OK;
    }

    // Haven't reached a timeout yet; continue scanning
    return ZX_OK;

timer_fail:
    errorf("could not set scan timer: %d\n", status);
    status = SendScanConfirm();
    Reset();
    return status;
}

zx_status_t Scanner::HandleError(zx_status_t error_code) {
    debugfn();
    resp_ = wlan_mlme::ScanConfirm::New();
    // TODO(tkilbourn): report the error code somehow
    resp_->result_code = wlan_mlme::ScanResultCodes::NOT_SUPPORTED;
    return SendScanConfirm();
}

zx::time Scanner::InitialTimeout() const {
    if (req_->scan_type == wlan_mlme::ScanTypes::PASSIVE) {
        return channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        return channel_start_ + WLAN_TU(req_->probe_delay);
    }
}

// TODO(hahnr): support SSID list (IEEE Std 802.11-2016 11.1.4.3.2)
zx_status_t Scanner::SendProbeRequest() {
    debugfn();

    size_t body_payload_len = 128;  // TODO(porce): Revisit this value choice.
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<ProbeRequest>(&packet, body_payload_len);

    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const common::MacAddr& mymac = device_->GetState()->address();
    const common::MacAddr& bssid = common::MacAddr(req_->bssid.data());

    hdr->addr1 = common::kBcastMac;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid;
    // TODO(NET-556): Clarify 'Sequence' ownership of MLME and STA. Don't set sequence number for
    // now.
    hdr->sc.set_seq(0);
    FillTxInfo(&packet, *hdr);

    auto body = frame.body;
    ElementWriter w(body->elements, body_payload_len);

    if (!w.write<SsidElement>(req_->ssid->data())) {
        errorf("could not write ssid \"%s\" to probe request\n", req_->ssid->data());
        return ZX_ERR_IO;
    }

    // TODO(hahnr): determine these rates based on hardware
    // Rates (in Mbps): 1, 2, 5.5, 6, 9, 11, 12, 18
    std::vector<uint8_t> rates = {0x02, 0x04, 0x0b, 0x0c, 0x12, 0x16, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("could not write supported rates\n");
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 24, 36, 48, 54
    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        return ZX_ERR_IO;
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(body->Validate(w.size()));

    // Update the length with final values
    body_payload_len = w.size();
    // TODO(porce): implement methods to replace sizeof(ProbeRequest) with body.some_len()
    frame.body_len = sizeof(ProbeRequest) + body_payload_len;
    size_t frame_len = hdr->len() + frame.body_len;
    zx_status_t status = packet->set_len(frame_len);
    if (status != ZX_OK) {
        errorf("could not set packet length to %zu: %d\n", frame_len, status);
        return status;
    }

    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) { errorf("could not send probe request packet: %d\n", status); }

    return status;
}

// TODO(hahnr): Move to service.cpp.
zx_status_t Scanner::SendScanConfirm() {
    debugfn();

    nbrs_bss_.ForEach([this](fbl::RefPtr<Bss> bss) {
        if (req_->ssid->size() == 0 || req_->ssid == bss->SsidToString()) {
            debugbss("%s\n", bss->ToString().c_str());
            resp_->bss_description_set->push_back(bss->ToFidl());
        }
    });

    // TODO(FIDL-2): replace this when we can get the size of the serialized response.
    size_t buf_len = 16384;
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), wlan_mlme::Method::SCAN_confirm, resp_.get());
    if (status != ZX_OK) {
        errorf("could not serialize ScanResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    nbrs_bss_.Clear();  // TODO(porce): Decouple BSS management from Scanner.
    return status;
}

}  // namespace wlan
