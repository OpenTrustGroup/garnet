// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "dispatcher.h"

#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/wlan.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/packet.h>
#include <zircon/compiler.h>
#include <zx/channel.h>
#include <zx/port.h>

#include <mutex>
#include <thread>

typedef struct zx_port_packet zx_port_packet_t;

namespace wlan {

class Timer;

class Device;
using WlanBaseDevice = ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>;

class Device : public WlanBaseDevice,
               public ddk::EthmacProtocol<Device>,
               public ddk::WlanmacIfc<Device>,
               public DeviceInterface {
   public:
    Device(zx_device_t* device, wlanmac_protocol_t* wlanmac_proto);
    ~Device();

    zx_status_t Bind();

    // ddk::Device methods
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    // ddk::WlanmacIfc methods
    void WlanmacStatus(uint32_t status);
    void WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info);
    void WlanmacCompleteTx(wlan_tx_packet_t* pkt, zx_status_t status);

    // ddk::EthmacProtocol methods
    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    zx_status_t EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) __TA_EXCLUDES(lock_);
    void EthmacStop() __TA_EXCLUDES(lock_);
    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf);
    zx_status_t EthmacSetParam(uint32_t param, int32_t value, void* data);

    // DeviceInterface methods
    zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) override final;
    zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet) override final;
    zx_status_t SendWlan(fbl::unique_ptr<Packet> packet) override final;
    zx_status_t SendService(fbl::unique_ptr<Packet> packet) override final;
    zx_status_t SetChannel(wlan_channel_t chan) override final;
    zx_status_t SetStatus(uint32_t status) override final;
    zx_status_t ConfigureBss(wlan_bss_config_t* cfg) override final;
    zx_status_t SetKey(wlan_key_config_t* key_config) override final;
    fbl::RefPtr<DeviceState> GetState() override final;
    const wlanmac_info_t& GetWlanInfo() const override final;

   private:
    enum class DevicePacket : uint64_t {
        kShutdown,
        kPacketQueued,
    };

    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer);
    template <typename T>
    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer,
                                          const T& ctrl_data) {
        auto packet = PreparePacket(data, length, peer);
        if (packet != nullptr) { packet->CopyCtrlFrom(ctrl_data); }
        return packet;
    }

    zx_status_t QueuePacket(fbl::unique_ptr<Packet> packet) __TA_EXCLUDES(packet_queue_lock_);

    void MainLoop();
    void ProcessChannelPacketLocked(const zx_port_packet_t& pkt) __TA_REQUIRES(lock_);
    zx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    zx_status_t QueueDevicePortPacket(DevicePacket id);

    zx_status_t GetChannel(zx::channel* out) __TA_EXCLUDES(lock_);
    void SetStatusLocked(uint32_t status);

    ddk::WlanmacProtocolProxy wlanmac_proxy_;
    fbl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_;

    wlanmac_info_t wlanmac_info_ = {};
    fbl::RefPtr<DeviceState> state_;

    std::mutex lock_;
    std::thread work_thread_;
    zx::port port_;

    Dispatcher dispatcher_ __TA_GUARDED(lock_);

    bool dead_ __TA_GUARDED(lock_) = false;
    zx::channel channel_ __TA_GUARDED(lock_);

    std::mutex packet_queue_lock_;
    PacketQueue packet_queue_ __TA_GUARDED(packet_queue_lock_);
};

}  // namespace wlan
