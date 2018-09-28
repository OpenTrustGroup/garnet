// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/element.h>
#include <wlan/common/logging.h>
#include <wlan/protocol/info.h>
#include <wlan/protocol/mac.h>
#include <zircon/assert.h>
#include <zircon/types.h>
#include <zx/time.h>

#include <cstdio>

namespace wlan {

static constexpr uint8_t kHtNumMcs = 32;  // Only support MCS 0-31
static constexpr uint8_t kHtNumUniqueMcs = 8;
static constexpr uint8_t kErpNumTxVector = 8;

static constexpr tx_vec_idx_t kInvalidTxVectorIdx = WLAN_TX_VECTOR_IDX_INVALID;

// Extend the definition of MCS index for ERP
// OFDM/ERP-OFDM, represented by WLAN_PHY_ERP:
// 0: BPSK,   1/2 -> Data rate  6 Mbps
// 1: BPSK,   3/4 -> Data rate  9 Mbps
// 2: QPSK,   1/2 -> Data rate 12 Mbps
// 3: QPSK,   3/4 -> Data rate 18 Mbps
// 4: 16-QAM, 1/2 -> Data rate 24 Mbps
// 5: 16-QAM, 3/4 -> Data rate 36 Mbps
// 6: 64-QAM, 2/3 -> Data rate 48 Mbps
// 7: 64-QAM, 3/4 -> Data rate 54 Mbps
// DSSS, HR/DSSS, and ERP-DSSS/CCK, reprsented by WLAN_PHY_DSSS and WLAN_PHY_CCK
// 0:  2 -> 1   Mbps DSSS
// 1:  4 -> 2   Mbps DSSS
// 2: 11 -> 5.5 Mbps CCK
// 3: 22 -> 11  Mbps CCK

struct TxVector {
    PHY phy;
    GI gi;
    CBW cbw;
    // number of spatial streams, for VHT and beyond
    uint8_t nss;
    // For HT,  see IEEE 802.11-2016 Table 19-27
    // For VHT, see IEEE 802.11-2016 Table 21-30
    // For ERP, see FromSupportedRate() below (Fuchsia extension)
    uint8_t mcs_idx;

    static zx_status_t FromSupportedRate(const SupportedRate& rate, TxVector* tx_vec);
    static zx_status_t FromIdx(tx_vec_idx_t idx, TxVector* tx_vec);

    bool IsValid() const;
    zx_status_t ToIdx(tx_vec_idx_t* idx) const;
};

bool operator==(const TxVector& lhs, const TxVector& rhs);
bool operator!=(const TxVector& lhs, const TxVector& rhs);
}  // namespace wlan
