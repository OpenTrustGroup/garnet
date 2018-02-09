// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/usb-request.h>
#include <ddktl/device.h>
#include <ddktl/protocol/wlan.h>
#include <driver/usb.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zx/time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace ralink {

template <uint16_t A> class Register;
template <uint8_t A> class BbpRegister;
template <uint8_t A> class RfcsrRegister;
template <uint16_t A> class EepromField;
enum KeyMode : uint8_t;
enum KeyType : uint8_t;
struct TxPacket;

class Device : public ddk::Device<Device, ddk::Unbindable>, public ddk::WlanmacProtocol<Device> {
   public:
    Device(zx_device_t* device, usb_protocol_t* usb, uint8_t bulk_in,
           std::vector<uint8_t>&& bulk_out);
    ~Device();

    zx_status_t Bind();

    // ddk::Device methods
    void DdkUnbind();
    void DdkRelease();

    // ddk::WlanmacProtocol methods
    zx_status_t WlanmacQuery(uint32_t options, wlanmac_info_t* info);
    zx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy);
    void WlanmacStop();
    zx_status_t WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt);
    zx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan);
    zx_status_t WlanmacSetBss(uint32_t options, const uint8_t mac[6], uint8_t type);
    zx_status_t WlanmacConfigureBss(uint32_t options, wlan_bss_config_t* config);
    zx_status_t WlanmacSetKey(uint32_t options, wlan_key_config_t* key_config);

   private:
    struct TxCalibrationValues {
        uint8_t gain_cal_tx0 = 0;
        uint8_t phase_cal_tx0 = 0;
        uint8_t gain_cal_tx1 = 0;
        uint8_t phase_cal_tx1 = 0;
    };

    // RF register values defined per channel number
    struct RfVal {
        RfVal() : channel(0), N(0), R(0), K(0), mod(0) {}

        RfVal(int channel, uint32_t N, uint32_t R, uint32_t K)
            : channel(channel), N(N), R(R), K(K), mod(0) {}

        RfVal(int channel, uint32_t N, uint32_t R, uint32_t K, uint32_t mod)
            : channel(channel), N(N), R(R), K(K), mod(mod) {}

        int channel;
        uint32_t N;
        uint32_t R;
        uint32_t K;
        uint32_t mod;

        TxCalibrationValues cal_values;

        int8_t default_power1 = 0;
        int8_t default_power2 = 0;
        int8_t default_power3 = 0;
    };

    struct RegInitValue {
        RegInitValue(uint8_t addr, uint8_t val) : addr(addr), val(val) {}
        uint8_t addr;
        uint8_t val;
    };

    // Configure RfVal tables
    zx_status_t InitializeRfVal();

    // read and write general registers
    zx_status_t ReadRegister(uint16_t offset, uint32_t* value);
    template <uint16_t A> zx_status_t ReadRegister(Register<A>* reg);
    zx_status_t WriteRegister(uint16_t offset, uint32_t value);
    template <uint16_t A> zx_status_t WriteRegister(const Register<A>& reg);

    // read and write the eeprom
    zx_status_t ReadEeprom();
    zx_status_t ReadEepromField(uint16_t addr, uint16_t* value);
    zx_status_t ReadEepromByte(uint16_t addr, uint8_t* value);
    template <uint16_t A> zx_status_t ReadEepromField(EepromField<A>* field);
    template <uint16_t A> zx_status_t WriteEepromField(const EepromField<A>& field);
    zx_status_t ValidateEeprom();

    // read and write baseband processor registers
    zx_status_t ReadBbp(uint8_t addr, uint8_t* val);
    template <uint8_t A> zx_status_t ReadBbp(BbpRegister<A>* reg);
    zx_status_t WriteBbp(uint8_t addr, uint8_t val);
    template <uint8_t A> zx_status_t WriteBbp(const BbpRegister<A>& reg);
    zx_status_t WriteBbpGroup(const std::vector<RegInitValue>& regs);
    zx_status_t WaitForBbp();

    // write glrt registers
    zx_status_t WriteGlrt(uint8_t addr, uint8_t val);
    zx_status_t WriteGlrtGroup(const std::vector<RegInitValue>& regs);
    zx_status_t WriteGlrtBlock(uint8_t values[], size_t size, size_t offset);

    // read and write rf registers
    zx_status_t ReadRfcsr(uint8_t addr, uint8_t* val);
    template <uint8_t A> zx_status_t ReadRfcsr(RfcsrRegister<A>* reg);
    zx_status_t WriteRfcsr(uint8_t addr, uint8_t val);
    template <uint8_t A> zx_status_t WriteRfcsr(const RfcsrRegister<A>& reg);
    zx_status_t WriteRfcsrGroup(const std::vector<RegInitValue>& regs);

    // send a command to the MCU
    zx_status_t McuCommand(uint8_t command, uint8_t token, uint8_t arg0, uint8_t arg1);

    // hardware encryption
    uint8_t DeriveSharedKeyIndex(uint8_t bss_idx, uint8_t key_idx);
    KeyMode MapIeeeCipherSuiteToKeyMode(const uint8_t cipher_oui[3], uint8_t cipher_type);
    zx_status_t WriteKey(const uint8_t key[], size_t key_len, uint16_t offset, KeyMode mode);
    zx_status_t WritePairwiseKey(uint8_t wcid, const uint8_t key[], size_t key_len, KeyMode mode);
    zx_status_t WriteSharedKey(uint8_t skey, const uint8_t key[], size_t key_len, KeyMode mode);
    zx_status_t WriteSharedKeyMode(uint8_t skey, KeyMode mode);
    zx_status_t ResetIvEiv(uint8_t wcid, uint8_t key_id, KeyMode mode);
    zx_status_t WriteWcid(uint8_t wcid, const uint8_t mac[]);
    zx_status_t WriteWcidAttribute(uint8_t bss_idx, uint8_t wcid, KeyMode mode, KeyType type);
    // resets all security aspects for a given WCID and shared key as well as their keys.
    zx_status_t ResetWcid(uint8_t wcid, uint8_t skey, uint8_t key_type);

    // initialization functions
    zx_status_t LoadFirmware();
    zx_status_t EnableRadio();
    zx_status_t InitRegisters();
    zx_status_t InitBbp();
    zx_status_t InitBbp5592();
    zx_status_t InitBbp5390();
    zx_status_t InitRfcsr();

    zx_status_t DetectAutoRun(bool* autorun);
    zx_status_t DisableWpdma();
    zx_status_t WaitForMacCsr();
    zx_status_t SetRxFilter();
    zx_status_t AdjustFreqOffset();
    zx_status_t NormalModeSetup();
    zx_status_t StartQueues();
    zx_status_t StopRxQueue();
    zx_status_t SetupInterface();

    zx_status_t LookupRfVal(const wlan_channel_t& chan, RfVal* rf_val);
    zx_status_t ConfigureChannel(const wlan_channel_t& chan);
    zx_status_t ConfigureChannel5390(const wlan_channel_t& chan);
    zx_status_t ConfigureChannel5592(const wlan_channel_t& chan);

    zx_status_t ConfigureTxPower(const wlan_channel_t& chan);

    template <typename R, typename Predicate>
    zx_status_t BusyWait(R* reg, Predicate pred, zx::duration delay = kDefaultBusyWait);

    void HandleRxComplete(usb_request_t* request);
    void HandleTxComplete(usb_request_t* request);

    zx_status_t FillUsbTxPacket(TxPacket* usb_packet, wlan_tx_packet_t* wlan_packet);
    uint8_t LookupTxWcid(const uint8_t* addr1, bool protected_frame);
    zx_status_t ConfigureBssBeacon(uint32_t options, wlan_tx_packet_t* bcn_pkt);

    static void ReadRequestComplete(usb_request_t* request, void* cookie);
    static void WriteRequestComplete(usb_request_t* request, void* cookie);

    size_t tx_pkt_len(wlan_tx_packet_t* pkt);
    size_t txwi_len();
    size_t align_pad_len(wlan_tx_packet_t* pkt);
    size_t terminal_pad_len();
    size_t usb_tx_pkt_len(wlan_tx_packet_t* pkt);

    usb_protocol_t usb_;
    fbl::unique_ptr<ddk::WlanmacIfcProxy> wlanmac_proxy_ __TA_GUARDED(lock_);

    uint8_t rx_endpt_ = 0;
    std::vector<uint8_t> tx_endpts_;

    constexpr static size_t kEepromSize = 0x0100;
    std::array<uint16_t, kEepromSize> eeprom_ = {};

    constexpr static zx::duration kDefaultBusyWait = zx::usec(100);

    // constants read out of the device
    uint16_t rt_type_ = 0;
    uint16_t rt_rev_ = 0;
    uint16_t rf_type_ = 0;

    uint8_t mac_addr_[ETH_MAC_SIZE];

    bool has_external_lna_2g_ = false;
    bool has_external_lna_5g_ = false;

    uint8_t tx_path_ = 0;
    uint8_t rx_path_ = 0;

    uint8_t antenna_diversity_ = 0;

    // key: 20MHz channel number
    // val: RF parameters for the channel
    std::map<int, RfVal> rf_vals_;

    // cfg_chan_ is what is configured (from higher layers)
    // TODO(porce): Define oper_chan_ to read from the registers directly
    wlan_channel_t cfg_chan_ = wlan_channel_t{
        .primary = 0,
        .cbw = CBW20,
    };
    uint16_t lna_gain_ = 0;
    uint8_t bg_rssi_offset_[3] = {};
    uint8_t bssid_[6];

    std::mutex lock_;
    std::vector<usb_request_t*> free_write_reqs_ __TA_GUARDED(lock_);
};

}  // namespace ralink
