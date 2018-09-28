// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/sequence.h>

#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/time.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/frame_validation.h>
#include <wlan/mlme/packet.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>

namespace wlan {

template <typename H, typename B> class Frame;
template <typename H, typename B> class FrameView;

template <typename H, typename B> class VerifiedFrameType {
   public:
    static VerifiedFrameType<H, B> Valid(FrameView<H, B> frame) {
        ZX_DEBUG_ASSERT(!frame.IsEmpty());
        return VerifiedFrameType<H, B>(frame);
    }

    static VerifiedFrameType<H, B> Invalid() { return VerifiedFrameType<H, B>(FrameView<H, B>()); }

    FrameView<H, B> CheckLength() const {
        if (!IsValid()) { return frame_; }

        if (!frame_.HasValidLen()) { return FrameView<H, B>(); }
        return frame_;
    }

    bool IsValid() const { return !frame_.IsEmpty(); }
    operator bool() const { return IsValid(); }

   private:
    explicit VerifiedFrameType(FrameView<H, B> frame) : frame_(frame) {}

    FrameView<H, B> frame_;
};

// A temporary representation of a frame.
template <typename Header, typename Body = UnknownBody> class FrameView {
   public:
    explicit FrameView(const Packet* pkt, size_t offset = 0) : data_offset_(offset), pkt_(pkt) {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
        ZX_DEBUG_ASSERT(pkt->len() >= offset);
    }

    FrameView() : data_offset_(0), pkt_(nullptr) {}

    static VerifiedFrameType<Header, Body> CheckType(const Packet* pkt, size_t offset = 0) {
        if (!is_valid_frame_type<Header, Body>(pkt, offset)) {
            return VerifiedFrameType<Header, Body>::Invalid();
        }

        return VerifiedFrameType<Header, Body>::Valid(FrameView<Header, Body>(pkt, offset));
    }

    template <typename NewBody> VerifiedFrameType<Header, NewBody> CheckBodyType() const {
        return FrameView<Header, NewBody>::CheckType(pkt_, data_offset_);
    }

    // Skips the header on the underlying data of the frame.
    // Method has no effect when called on empty frames.
    FrameView<Body, UnknownBody> SkipHeader() const {
        if (IsEmpty()) { return {}; }


        ZX_DEBUG_ASSERT(body_offset() <= pkt_->len());
        return FrameView<Body, UnknownBody>(pkt_, body_offset());
    }

    // Verifies and treats the underlying data as a different frame type.
    template <typename NewHeader, typename NewBody = UnknownBody>
    VerifiedFrameType<NewHeader, NewBody> As() const {
        return FrameView<NewHeader, NewBody>::CheckType(pkt_, data_offset_);
    }

    // Advances the underlying data by `len` bytes and returns a frame of unknown type.
    // Returns an empty frame if the underlying data is too short.
    FrameView<UnknownBody, UnknownBody> AdvanceBy(size_t len) const {
        if (IsEmpty() || pkt_->len() - data_offset_ < len) { return {}; }
        return FrameView<UnknownBody, UnknownBody>(pkt_, data_offset_ + len);
    }

    Frame<Header, Body> IntoOwned(fbl::unique_ptr<Packet> pkt) const {
        ZX_DEBUG_ASSERT(pkt != nullptr && pkt.get() == pkt_);
        return Frame<Header, Body>(data_offset_, fbl::move(pkt));
    }

    const uint8_t* data() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        return pkt_->data();
    }

    const uint8_t* body_data() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        auto body = pkt_->field<uint8_t>(body_offset());
        ZX_DEBUG_ASSERT(body != nullptr);
        return body;
    }

    const Header* hdr() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        auto hdr = pkt_->field<Header>(hdr_offset());
        ZX_DEBUG_ASSERT(hdr != nullptr);
        return hdr;
    }

    const Body* body() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        auto body = pkt_->field<Body>(body_offset());
        ZX_DEBUG_ASSERT(body != nullptr);
        return body;
    }

    size_t body_len() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        size_t offset = body_offset();
        ZX_DEBUG_ASSERT(offset <= pkt_->len());
        if (pkt_->len() < offset) { return 0; }
        return pkt_->len() - offset;
    }

    size_t len() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        if (pkt_->len() < hdr_offset()) { return 0; }
        return pkt_->len() - hdr_offset();
    }

    bool has_rx_info() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        if (!is_mac_hdr<Header>::value) { return false; }

        return pkt_->has_ctrl_data<wlan_rx_info_t>();
    }

    const wlan_rx_info_t* rx_info() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
        ZX_DEBUG_ASSERT(has_rx_info());
        static_assert(is_mac_hdr<Header>::value, "only MAC frame can carry rx_info");

        return pkt_->ctrl_data<wlan_rx_info_t>();
    }

    bool HasValidLen() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        return is_valid_frame_length<Header, Body>(pkt_, data_offset_);
    }

    bool IsEmpty() const { return pkt_ == nullptr; }
    operator bool() const { return !IsEmpty(); }

    // Advances the frame such that it points to the beginning of its previous Body.
    // Example:
    //     DataFrameView<> data_frame(packet_ptr);
    //     FrameView<LlcHeader> llc_frame = data_frame.NextFrame<LlcHeader>();
    //     FrameView<Eapol> eapol_frame = llc_frame.NextFrame<Eapol>();
    // TODO(hahnr): This should be called NextHdr instead.
    template <typename NextH = Body, typename NextB = UnknownBody>
    FrameView<NextH, NextB> NextFrame() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        return FrameView<NextH, NextB>(pkt_, body_offset());
    }

    // Allows to change the representation of the frame's body. The resulting frame's length should
    // be verified before working with it. One would typically use this method after verifying that
    // an "unknown" body is supposed to be of a certain type. Because this method takes a frame's
    // offset into account, it should be used when specializing frames, rather than taking the
    // frame's Packet and constructing a new Frame yourself which can be error prone when working
    // with advanced frames. For example, avoid doing this:
    //     FrameView<LlcHeader, UnknownBody> llc_frame = data_frame.NextFrame();
    //     FrameView<LlcHeader, EapolHeader> llc_eapol_frame(llc_frame.take());
    //     PROBLEM: llc_eapol_frame.body() is *NOT* pointing to the EAPOL header
    //              because the frame's offset got lost
    //
    // Instead use this method:
    //     FrameView<LlcHeader, UnknownBody> llc_frame = data_frame.NextFrame();
    //     FrameView<LlcHeader, EapolHeader> llc_eapol_frame = llc_frame.Specialize<EapolHeader>();
    //     ...
    template <typename NewBody> FrameView<Header, NewBody> Specialize() const {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        return FrameView<Header, NewBody>(pkt_, hdr_offset());
    }

    size_t hdr_offset() const { return data_offset_; }

    size_t body_offset() const {
        auto padding_func = get_packet_padding_func<Header>(pkt_);
        return hdr_offset() + padding_func(hdr()->len());
    }

   private:
    size_t data_offset_ = 0;
    const Packet* pkt_;
};

template <typename Header, typename Body = UnknownBody> class Frame {
   public:
    explicit Frame(fbl::unique_ptr<Packet> pkt) : data_offset_(0), pkt_(fbl::move(pkt)) {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
    }

    Frame(size_t offset, fbl::unique_ptr<Packet> pkt) : data_offset_(offset), pkt_(fbl::move(pkt)) {
        ZX_DEBUG_ASSERT(pkt_ != nullptr);
    }

    Frame() : data_offset_(0), pkt_(nullptr) {}

    Header* hdr() {
        ZX_DEBUG_ASSERT(!IsEmpty());

        auto hdr = pkt_->mut_field<Header>(View().hdr_offset());
        ZX_DEBUG_ASSERT(hdr != nullptr);
        return hdr;
    }

    const Header* hdr() const { return View().hdr(); }

    Body* body() {
        ZX_DEBUG_ASSERT(!IsEmpty());

        auto body = pkt_->mut_field<Body>(View().body_offset());
        ZX_DEBUG_ASSERT(body != nullptr);
        return body;
    }

    const Body* body() const { return View().body(); }

    size_t body_len() const { return View().body_len(); }

    zx_status_t set_body_len(size_t len) {
        ZX_DEBUG_ASSERT(!IsEmpty());
        ZX_DEBUG_ASSERT(len <= pkt_->len());

        return pkt_->set_len(View().body_offset() + len);
    }

    size_t len() const { return View().len(); }

    zx_status_t FillTxInfo(uint8_t cbw = CBW20, uint16_t phy = WLAN_PHY_OFDM) {
        static_assert(is_mac_hdr<Header>::value, "only MAC frame can carry tx_info");
        ZX_DEBUG_ASSERT(pkt_ != nullptr);

        wlan_tx_info_t txinfo = {
            .tx_flags = 0x0,
            .valid_fields = WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH | WLAN_TX_INFO_VALID_MCS,
            .phy = phy,
            .cbw = cbw,
        };

        // TODO(porce): Implement rate selection.
        auto fc = pkt_->field<FrameControl>(0);
        switch (fc->type()) {
        // Outgoing data frames.
        case FrameType::kData:
            txinfo.mcs = 0x7;
            break;
        // Outgoing management and control frames.
        default:
            txinfo.mcs = 0x3;  // TODO(NET-645): Choose an optimal MCS
            break;
        }

        if (fc->protected_frame()) {
            txinfo.tx_flags |= WLAN_TX_INFO_FLAGS_PROTECTED;
        }

        pkt_->CopyCtrlFrom(txinfo);
        return ZX_OK;
    }

    bool HasValidLen() const {
        if (IsEmpty()) { return false; }
        return View().HasValidLen();
    }

    // Similar to `FrameView::NextFrame()` but consumes this Frame.
    template <typename NextH = Body, typename NextB = UnknownBody> Frame<NextH, NextB> NextFrame() {
        return Frame<NextH, NextB>(View().body_offset(), Take());
    }

    // Similar to `FrameView::Specialize()` but consumes this Frame.
    template <typename NewBody> Frame<Header, NewBody> Specialize() {
        return Frame<Header, NewBody>(View().hdr_offset(), Take());
    }

    // `true` if the frame was 'taken' and should no longer be used.
    bool IsEmpty() const { return pkt_ == nullptr; }

    // Returns the Frame's underlying Packet. The Frame will no longer own the Packet and
    // will be `empty` from that moment on and should no longer be used.
    fbl::unique_ptr<Packet> Take() {
        ZX_DEBUG_ASSERT(!IsEmpty());
        return fbl::move(pkt_);
    }

    FrameView<Header, Body> View() const {
        ZX_DEBUG_ASSERT(!IsEmpty());

        return FrameView<Header, Body>(pkt_.get(), data_offset_);
    }

   private:
    size_t data_offset_;
    fbl::unique_ptr<Packet> pkt_;
};

// Frame which contains a known header but unknown payload.
using EthFrame = Frame<EthernetII>;
template <typename T = UnknownBody> using MgmtFrame = Frame<MgmtFrameHeader, T>;
template <typename T = UnknownBody> using CtrlFrame = Frame<CtrlFrameHdr, T>;
template <typename T = UnknownBody> using DataFrame = Frame<DataFrameHeader, T>;

using EthFrameView = FrameView<EthernetII>;
template <typename T = UnknownBody> using MgmtFrameView = FrameView<MgmtFrameHeader, T>;
template <typename T = UnknownBody> using CtrlFrameView = FrameView<CtrlFrameHdr, T>;
template <typename T = UnknownBody> using DataFrameView = FrameView<DataFrameHeader, T>;

// TODO(hahnr): This isn't a great location for these definitions.
using aid_t = size_t;
static constexpr aid_t kGroupAdressedAid = 0;
static constexpr aid_t kMaxBssClients = 2008;
static constexpr aid_t kUnknownAid = kMaxBssClients + 1;

template <typename Body>
zx_status_t CreateMgmtFrame(MgmtFrame<Body>* frame, size_t body_payload_len = 0,
                            bool has_ht_ctrl = false);

seq_t NextSeqNo(const MgmtFrameHeader& hdr, Sequence* seq);
seq_t NextSeqNo(const MgmtFrameHeader& hdr, uint8_t aci, Sequence* seq);
seq_t NextSeqNo(const DataFrameHeader& hdr, Sequence* seq);

void SetSeqNo(MgmtFrameHeader* hdr, Sequence* seq);
void SetSeqNo(MgmtFrameHeader* hdr, uint8_t aci, Sequence* seq);
void SetSeqNo(DataFrameHeader* hdr, Sequence* seq);

using MsduCallback = std::function<void(FrameView<LlcHeader>, size_t)>;

// Returns a list of all LLC frames carried in an AMSDU data frame.
zx_status_t DeaggregateAmsdu(const DataFrameView<AmsduSubframeHeader>&, MsduCallback);

}  // namespace wlan
