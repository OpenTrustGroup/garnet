// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/eapol.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <zircon/types.h>

namespace wlan {

class BaseState;

class RemoteClient : public RemoteClientInterface {
   public:
    struct Listener {
        virtual zx_status_t HandleClientDeauth(const common::MacAddr& client) = 0;
        virtual void HandleClientDisassociation(aid_t aid) = 0;
        virtual void HandleClientBuChange(const common::MacAddr& client, size_t bu_count) = 0;
    };

    RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                 Listener* listener, const common::MacAddr& addr);
    ~RemoteClient();

    // RemoteClientInterface implementation
    aid_t GetAid() override;
    void HandleTimeout() override;
    void HandleAnyEthFrame(EthFrame&& frame) override;
    void HandleAnyMgmtFrame(MgmtFrame<>&& frame) override;
    void HandleAnyDataFrame(DataFrame<>&& frame) override;
    void HandleAnyCtrlFrame(CtrlFrame<>&& frame) override;
    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& mlme_msg) override;
    zx_status_t SendAuthentication(status_code::StatusCode result);
    zx_status_t SendAssociationResponse(aid_t aid, status_code::StatusCode result);
    zx_status_t SendDeauthentication(reason_code::ReasonCode reason_code);
    zx_status_t SendAddBaRequest();
    zx_status_t SendAddBaResponse(const AddBaRequestFrame& rx_frame);

    uint8_t GetTid();
    uint8_t GetTid(const EthFrame& frame);

    // Enqueues an ethernet frame which can be sent at a later point in time.
    zx_status_t EnqueueEthernetFrame(const EthFrame& frame);
    zx_status_t DequeueEthernetFrame(fbl::unique_ptr<Packet>* out_packet);
    bool HasBufferedFrames() const;
    zx_status_t ConvertEthernetToDataFrame(const EthFrame& frame,
                                           fbl::unique_ptr<Packet>* out_frame);

    void MoveToState(fbl::unique_ptr<BaseState> state);
    void ReportBuChange(size_t bu_count);
    void ReportDeauthentication();
    void ReportDisassociation(aid_t aid);

    // Note: There can only ever be one timer running at a time.
    // TODO(hahnr): Evolve this to support multiple timeouts at the same time.
    zx_status_t StartTimer(zx::time deadline);
    zx_status_t CancelTimer();
    zx::time CreateTimerDeadline(wlan_tu_t tus);
    bool IsDeadlineExceeded(zx::time deadline);

    DeviceInterface* device() { return device_; }
    BssInterface* bss() { return bss_; }
    const common::MacAddr& addr() { return addr_; }

   private:
    zx_status_t WriteHtCapabilities(ElementWriter* w);
    zx_status_t WriteHtOperation(ElementWriter* w);

    // Maximum number of packets buffered while the client is in power saving
    // mode.
    // TODO(NET-687): Find good BU limit.
    static constexpr size_t kMaxPowerSavingQueueSize = 30;

    Listener* const listener_;
    DeviceInterface* const device_;
    BssInterface* const bss_;
    const common::MacAddr addr_;
    const fbl::unique_ptr<Timer> timer_;
    // Queue which holds buffered `EthernetII` packets while the client is in
    // power saving mode.
    PacketQueue bu_queue_;
    fbl::unique_ptr<BaseState> state_;
};

class BaseState {
   public:
    BaseState(RemoteClient* client) : client_(client) {}
    virtual ~BaseState() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual aid_t GetAid() { return kUnknownAid; }
    virtual void HandleTimeout() {}
    virtual zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) { return ZX_OK; }
    virtual void HandleAnyDataFrame(DataFrame<>&&) {}
    virtual void HandleAnyMgmtFrame(MgmtFrame<>&&) {}
    virtual void HandleAnyCtrlFrame(CtrlFrame<>&&) {}
    virtual void HandleEthFrame(EthFrame&&) {}

    virtual const char* name() const = 0;

   protected:
    template <typename S, typename... Args> void MoveToState(Args&&... args);

    RemoteClient* const client_;
};

class DeauthenticatingState : public BaseState {
   public:
    DeauthenticatingState(RemoteClient* client);

    void OnEnter() override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Deauthenticating";
};

class DeauthenticatedState : public BaseState {
   public:
    DeauthenticatedState(RemoteClient* client);

    inline const char* name() const override { return kName; }

    void HandleAnyMgmtFrame(MgmtFrame<>&&) override;

   private:
    static constexpr const char* kName = "Deauthenticated";
};

class AuthenticatingState : public BaseState {
   public:
    AuthenticatingState(RemoteClient* client, MgmtFrame<Authentication>&& frame);

    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Authenticating";
    zx_status_t FinalizeAuthenticationAttempt(const status_code::StatusCode st_code);
};

class AuthenticatedState : public BaseState {
   public:
    AuthenticatedState(RemoteClient* client);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout() override;
    void HandleAnyMgmtFrame(MgmtFrame<>&&) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Authenticated";

    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr wlan_tu_t kAuthenticationTimeoutTu = 1800000;  // ~30min

    void HandleAuthentication(MgmtFrame<Authentication>&&);
    void HandleAssociationRequest(MgmtFrame<AssociationRequest>&&);
    void HandleDeauthentication(MgmtFrame<Deauthentication>&&);

    zx::time auth_timeout_;
};

class AssociatingState : public BaseState {
   public:
    AssociatingState(RemoteClient* client, MgmtFrame<AssociationRequest>&& frame);

    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
    zx_status_t FinalizeAssociationAttempt(status_code::StatusCode st_code);

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Associating";

    uint16_t aid_;
};

class AssociatedState : public BaseState {
   public:
    AssociatedState(RemoteClient* client, uint16_t aid);

    void OnEnter() override;
    void OnExit() override;

    aid_t GetAid() override;
    void HandleTimeout() override;

    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
    void HandleAnyDataFrame(DataFrame<>&&) override;
    void HandleAnyMgmtFrame(MgmtFrame<>&&) override;
    void HandleAnyCtrlFrame(CtrlFrame<>&&) override;
    void HandleEthFrame(EthFrame&&) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Associated";

    zx_status_t HandleMlmeEapolReq(const MlmeMsg<::fuchsia::wlan::mlme::EapolRequest>& req);
    zx_status_t HandleMlmeSetKeysReq(const MlmeMsg<::fuchsia::wlan::mlme::SetKeysRequest>& req);

    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr wlan_tu_t kInactivityTimeoutTu = 300000;  // ~5min
    zx_status_t SendNextBu();
    void UpdatePowerSaveMode(const FrameControl& fc);

    void HandleAuthentication(MgmtFrame<Authentication>&&);
    void HandleAssociationRequest(MgmtFrame<AssociationRequest>&&);
    void HandleDeauthentication(MgmtFrame<Deauthentication>&&);
    void HandleDisassociation(MgmtFrame<Disassociation>&&);
    void HandleActionFrame(MgmtFrame<ActionFrame>&&);
    void HandleDataLlcFrame(DataFrame<LlcHeader>&&);
    void HandlePsPollFrame(CtrlFrame<PsPollFrame>&&);

    const uint16_t aid_;
    zx::time inactive_timeout_;
    // `true` if the client was active during the last inactivity timeout.
    bool active_;
    // `true` if the client entered Power Saving mode's doze state.
    bool dozing_;
    // `true` if a Deauthentication notification should be sent when leaving the
    // state.
    bool req_deauth_ = true;
    eapol::PortState eapol_controlled_port_ = eapol::PortState::kBlocked;
};

}  // namespace wlan
