// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/bss_client_map.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/sequence.h>

#include <wlan/common/macaddr.h>
#include <zircon/types.h>

namespace wlan {

class ObjectId;

// An infrastructure BSS which keeps track of its client and owned by the AP MLME.
class InfraBss : public BssInterface, public FrameHandler, public RemoteClient::Listener {
   public:
    InfraBss(DeviceInterface* device, fbl::unique_ptr<BeaconSender> bcn_sender,
             const common::MacAddr& bssid);
    virtual ~InfraBss();

    zx_status_t HandleTimeout(const common::MacAddr& client_addr);

    // BssInterface implementation
    const common::MacAddr& bssid() const override;
    uint64_t timestamp() override;
    zx_status_t AssignAid(const common::MacAddr& client, aid_t* out_aid) override;
    zx_status_t ReleaseAid(const common::MacAddr& client) override;
    fbl::unique_ptr<Buffer> GetPowerSavingBuffer(size_t len) override;

    seq_t NextSeq(const MgmtFrameHeader& hdr) override;
    seq_t NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) override;
    seq_t NextSeq(const DataFrameHeader& hdr) override;

   private:
    // FrameHandler implementation
    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                  const wlan_rx_info_t& rxinfo) override;

    // RemoteClient::Listener implementation
    void HandleClientStateChange(const common::MacAddr& client, RemoteClient::StateId from,
                                 RemoteClient::StateId to) override;
    void HandleClientBuChange(const common::MacAddr& client, size_t bu_count) override;

    zx_status_t CreateClientTimer(const common::MacAddr& client_addr,
                                  fbl::unique_ptr<Timer>* out_timer);

    const common::MacAddr bssid_;
    DeviceInterface* device_;
    fbl::unique_ptr<BeaconSender> bcn_sender_;
    bss::timestamp_t started_at_;
    BssClientMap clients_;
    Sequence seq_;
    TrafficIndicationMap tim_;
};

}  // namespace wlan
