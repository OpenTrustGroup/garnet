// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS;

enum CBW {
    // Channel Bandwidth. See IEEE 802.11-2016 21.2.4 Table 21-2
    // VHT notation

    CBW20 = 0,  // Default. Corresponds to SecondaryChannelOffset-None
    CBW40 = 1,
    CBW40ABOVE = CBW40,  // Corresponds to SecondaryChannelOffset-Above
    CBW40BELOW = 2,      // Corresponds to SecondaryChannelOffset-Below
    CBW80 = 3,
    CBW160 = 4,
    CBW80P80 = 5,  // Non-contiguous frequency segments

    CBW_COUNT,
};

typedef struct wlan_channel {
    uint8_t primary;
    uint8_t cbw;  // Channel Bandwidth
    uint8_t secondary80;
} wlan_channel_t;

enum {
    WLAN_RX_INFO_VALID_PHY = (1 << 0),
    WLAN_RX_INFO_VALID_DATA_RATE = (1 << 1),
    WLAN_RX_INFO_VALID_CHAN_WIDTH = (1 << 2),
    WLAN_RX_INFO_VALID_MCS = (1 << 3),
    WLAN_RX_INFO_VALID_RSSI = (1 << 4),
    WLAN_RX_INFO_VALID_RCPI = (1 << 5),
    WLAN_RX_INFO_VALID_SNR = (1 << 6),
    // Bits 7-31 reserved
};

// PHY values may be used in a bitfield (e.g., device capabilities) or as a value (e.g., rx or tx
// info).
enum PHY {
    WLAN_PHY_DSSS = (1 << 0),
    WLAN_PHY_CCK = (1 << 1),
    WLAN_PHY_ERP = (1 << 2),
    WLAN_PHY_OFDM = (1 << 2),
    WLAN_PHY_HT = (1 << 3),
    WLAN_PHY_VHT = (1 << 4),
};

// Guard Interval
enum GI {
    WLAN_GI_800NS = (1 << 0),  // all 802.11 phy
    WLAN_GI_400NS = (1 << 1),  // 802.11n/ac
    WLAN_GI_200NS = (1 << 2),  // 802.11n/ac
    WLAN_GI_3200NS = (1 << 3), // 802.11ax
    WLAN_GI_1600NS = (1 << 4), // 802.11ax
};

enum {
    WLAN_BSS_TYPE_INFRASTRUCTURE = 1,
    WLAN_BSS_TYPE_IBSS = 2, // Independent BSS
    WLAN_BSS_TYPE_PERSONAL = 3,
    WLAN_BSS_TYPE_MESH = 4,
    WLAN_BSS_TYPE_ANY_BSS = 5,
};

typedef struct wlan_bss_config {
    uint8_t bssid[6];
    // Whether this BSS is an infrastructure or independent BSS.
    uint8_t bss_type;
    // If 'remote' is 'true', the BSS is *not* managed by this device.
    bool remote;
} wlan_bss_config_t;

enum {
    // Device or driver implements scanning
    WLAN_DRIVER_FEATURE_SCAN_OFFLOAD = (1 << 0),
    // Device or driver implements rate selection. The data_rate and mcs fields of wlan_tx_info_t
    // should not be populated, unless the MLME wishes to force a given rate for a packet.
    WLAN_DRIVER_FEATURE_RATE_SELECTION = (1 << 1),
    // Device is not a physical device.
    WLAN_DRIVER_FEATURE_SYNTH = (1 << 2),
    // Driver supports transmission reports, and will use the wlanmac_ifc.report_tx_status()
    // callback to report the status of each queued transmission.
    WLAN_DRIVER_FEATURE_TX_STATUS_REPORT = (1 << 3),
};

// Mac roles: a device may support multiple roles, but an interface is instantiated with
// a single role.
enum {
    // Device operating as a non-AP station (i.e., a client of an AP).
    WLAN_MAC_ROLE_CLIENT = (1 << 0),
    // Device operating as an access point.
    WLAN_MAC_ROLE_AP = (1 << 1),
    // TODO: IBSS, PBSS, mesh
};

// Hardware capabilities.
// Some bits are inspired from IEEE Std 802.11-2016, 9.4.1.4
enum {
    WLAN_CAP_SHORT_PREAMBLE = (1 << 0),
    WLAN_CAP_SPECTRUM_MGMT = (1 << 1),
    WLAN_CAP_SHORT_SLOT_TIME = (1 << 2),
    WLAN_CAP_RADIO_MSMT = (1 << 3),
};

// HT capabilities. IEEE Std 802.11-2016, 9.4.2.56
typedef struct wlan_ht_caps {
    uint16_t ht_capability_info;
    uint8_t ampdu_params;
    union {
        uint8_t supported_mcs_set[16];
        struct {
            uint64_t rx_mcs_head;
            uint32_t rx_mcs_tail;
            uint32_t tx_mcs;
        } __PACKED mcs_set;
    };
    uint16_t ht_ext_capabilities;
    uint32_t tx_beamforming_capabilities;
    uint8_t asel_capabilities;
} __PACKED wlan_ht_caps_t;

// HT Operation. IEEE Std 802.11-2016,
typedef struct wlan_ht_op {
    uint8_t primary_chan;
    union {
        uint8_t info[5];
        struct {
            uint32_t head;
            uint8_t tail;
        } __PACKED;
    };
    union {
        uint8_t supported_mcs_set[16];
        struct {
            uint64_t rx_mcs_head;
            uint32_t rx_mcs_tail;
            uint32_t tx_mcs;
        } __PACKED basic_mcs_set;
    };
} __PACKED wlan_ht_op_t;

// VHT capabilities. IEEE Std 802.11-2016, 9.4.2.158
typedef struct wlan_vht_caps {
    uint32_t vht_capability_info;
    uint64_t supported_vht_mcs_and_nss_set;
} __PACKED wlan_vht_caps_t;

// VHT Operation. IEEE Std 802.11-2016, 9.4.2.159
typedef struct wlan_vht_op {
    uint8_t vht_cbw;
    uint8_t center_freq_seg0;
    uint8_t center_freq_seg1;
    uint16_t basic_mcs;
} __PACKED wlan_vht_op_t;

// Channels are numbered as in IEEE Std 802.11-2016, 17.3.8.4.2
// Each channel is defined as base_freq + 5 * n MHz, where n is between 1 and 200 (inclusive). Here
// n represents the channel number.
// Example:
//   Standard 2.4GHz channels:
//     base_freq = 2407 MHz
//     n = 1-14

#define WLAN_CHANNELS_MAX_LEN 64
typedef struct wlan_chan_list {
    uint16_t base_freq;
    // Each entry in this array represents a value of n in the above channel numbering formula.
    // The array size is roughly based on what is needed to represent the most common 5GHz
    // operating classes.
    // List up valid channels. A value of 0 indicates the end of the list if less than
    // WLAN_CHANNELS_MAX_LEN channels are defined.
    uint8_t channels[WLAN_CHANNELS_MAX_LEN];
} wlan_chan_list_t;

#define WLAN_BAND_DESC_MAX_LEN 16
#define WLAN_BASIC_RATES_MAX_LEN 12

typedef struct wlan_band_info {
    // Human-readable description of the band, for debugging.
    char desc[WLAN_BAND_DESC_MAX_LEN];
    // HT PHY capabilities.
    bool ht_supported;
    wlan_ht_caps_t ht_caps;
    // VHT PHY capabilities.
    bool vht_supported;
    wlan_vht_caps_t vht_caps;
    // Basic rates supported in this band, as defined in IEEE Std 802.11-2016, 9.4.2.3.
    // Each rate is given in units of 500 kbit/s, so 1 Mbit/s is represent as 0x02.
    uint8_t basic_rates[WLAN_BASIC_RATES_MAX_LEN];
    // Channels supported in this band.
    wlan_chan_list_t supported_channels;
} wlan_band_info_t;

// For now up to 2 bands are supported in order to keep the wlan_info struct a small, fixed
// size.
#define WLAN_MAX_BANDS 2

typedef struct wlan_info {
    uint8_t mac_addr[6];
    // Bitmask for MAC roles supported (WLAN_MAC_ROLE_*). For an interface, this will be a
    // single value.
    uint16_t mac_role;
    // Bitmask indicating the WLAN_PHY_* values supported by the hardware.
    uint16_t supported_phys;
    // Bitmask indicating the WLAN_DRIVER_FEATURE_* values supported by the driver and hardware.
    uint32_t driver_features;
    // Bitmask indicating WLAN_CAP_* capabilities. Note this differs from IEEE Std 802.11-2016, 9.4.1.4.
    uint32_t caps;
    // Supported bands.
    uint8_t num_bands;
    wlan_band_info_t bands[WLAN_MAX_BANDS];
} wlan_info_t;

// Information defined only within a context of association
// Beware the subtle interpretation of each field: they are designed to
// reflect the parameters safe to use within an association
// Many parameters do not distinguish Rx capability from Tx capability.
// In those cases, a capability is commonly applied to both Rx and Tx.
// Some parameters are distinctively for Rx only, and some are Tx only.
#define WLAN_MAC_SUPPORTED_RATES_MAX_LEN 8
#define WLAN_MAC_EXT_SUPPORTED_RATES_MAX_LEN 255
typedef struct wlan_assoc_ctx {
    uint8_t bssid[6];
    uint16_t aid;

    // IEEE Std 802.11-2016, 9.4.2.3
    uint8_t supported_rates_cnt;
    uint8_t supported_rates[WLAN_MAC_SUPPORTED_RATES_MAX_LEN];

    // IEEE Std 802.11-2016, 9.4.2.13
    uint8_t ext_supported_rates_cnt;
    uint8_t ext_supported_rates[WLAN_MAC_EXT_SUPPORTED_RATES_MAX_LEN];

    // IEEE Std 802.11-2016, 9.4.1.4
    uint8_t cap_info[2];

    // IEEE Std 802.11-2016, 9.4.2.56, 57
    // Rx MCS Bitmask in Supported MCS Set field represents the set of MCS
    // the peer can receive at from this device, considering this device's Tx capability.
    bool has_ht_cap;
    wlan_ht_caps_t ht_cap;
    bool has_ht_op;
    wlan_ht_op_t ht_op;

    // IEEE Std 802.11-2016, 9.4.2.158, 159
    bool has_vht_cap;
    wlan_vht_caps_t vht_cap;
    bool has_vht_op;
    wlan_vht_op_t vht_op;
} wlan_assoc_ctx_t;

__END_CDECLS;
