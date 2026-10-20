// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/platform-defs.h>
#include <zircon/types.h>

#include "mt8668-dts-pbus.h"

zx_status_t mt8668_board_wdt_register_from_dts(const mt8668_dts_pbus_t* dts) {
    static const mt8668_dts_pbus_device_t wdt = {
        .name = "wdt",
        .key = "mediatek,mt6881-wdt",
        .lookup = MT8668_DTS_LOOKUP_COMPATIBLE,
        .compatible = "mediatek,mt6881-wdt",
        .required_mmios = 1,
        .required_irqs = 0,
        .vid = PDEV_VID_GRT_MTKV8,
        .pid = PDEV_PID_MT8668,
        .did = PDEV_DID_GRT_WDT,
        .bti = MT8668_DTS_BTI_DEFAULT,
    };

    return mt8668_dts_pbus_add_device(dts, &wdt);
}
