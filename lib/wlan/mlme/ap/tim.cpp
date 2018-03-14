// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/tim.h>

namespace wlan {

TrafficIndicationMap::TrafficIndicationMap() {
    aid_bitmap_.Reset(kMaxBssClients);
}

void TrafficIndicationMap::SetTrafficIndication(aid_t aid, bool has_bu) {
    ZX_DEBUG_ASSERT(aid < kMaxBssClients);
    if (aid >= kMaxBssClients) {
        errorf("attempt setting traffic indication for invalid aid: %zu\n", aid);
        return;
    }

    if (has_bu) {
        aid_bitmap_.SetOne(aid);
    } else {
        aid_bitmap_.ClearOne(aid);
    }
}

zx_status_t TrafficIndicationMap::WritePartialVirtualBitmap(uint8_t* buf, size_t buf_len,
                                                            size_t* bitmap_len,
                                                            uint16_t* bitmap_offset) {
    size_t n1 = N1();
    size_t n2 = N2();

    *bitmap_len = 1 + (n2 - n1);
    if (buf_len < *bitmap_len) { return ZX_ERR_BUFFER_TOO_SMALL; }

    auto data = static_cast<const uint8_t*>(aid_bitmap_.StorageUnsafe()->GetData());
    memcpy(buf, data + n1, *bitmap_len);
    *bitmap_offset = n1 / 2;

    return ZX_OK;
}

size_t TrafficIndicationMap::N1() {
    size_t first_set_bit = aid_bitmap_.Scan(1, aid_bitmap_.size(), false);
    if (first_set_bit == aid_bitmap_.size()) {
        // No bit set.
        return 0;
    }
    size_t n1 = first_set_bit / 8;
    return n1 % 2 == 0 ? n1 : (n1 - 1);
}

size_t TrafficIndicationMap::N2() {
    // TODO(hahnr): Implement once ZX-1781 landed.
    // For now, let the bitmap be of the minimum one byte length (sufficient for testing).
    return N1();
}
}  // namespace wlan