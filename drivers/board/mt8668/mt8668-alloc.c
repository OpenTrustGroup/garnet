// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/platform-defs.h>
#include <zircon/types.h>

#include "mt8668-dts-pbus.h"

zx_status_t mt8668_board_guest_alloc_register_from_dts(const mt8668_dts_pbus_t* dts) {
    static const mt8668_dts_pbus_device_t guest_allocator = {
        .name = "guest_allocator",
        .key = "grt,guest-allocator",
        .lookup = MT8668_DTS_LOOKUP_COMPATIBLE,
        .compatible = "grt,guest-allocator",
        .required_mmios = 0,
        .required_irqs = 0,
        .vid = PDEV_VID_GENERIC,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_GRT_GUEST_MEMORY_ALLOCATOR,
        .bti = MT8668_DTS_BTI_NONE,
    };

    return mt8668_dts_pbus_add_device(dts, &guest_allocator);
}
