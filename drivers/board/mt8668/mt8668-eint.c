// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/platform-defs.h>
#include <zircon/types.h>

#include "mt8668-dts-pbus.h"

zx_status_t mt8668_board_eint_register_from_dts(const mt8668_dts_pbus_t* dts) {
    static const mt8668_dts_pbus_device_t eint = {
        .name = "mtk_eint",
        .key = "mediatek,mt6983-eint",
        .lookup = MT8668_DTS_LOOKUP_COMPATIBLE,
        .compatible = "mediatek,mt6983-eint",
        .required_mmios = 4,
        .required_irqs = 1,
        .vid = PDEV_VID_MTK,
        .pid = PDEV_PID_MT8668,
        .did = PDEV_DID_GRT_EINT,
        .bti = MT8668_DTS_BTI_DEFAULT,
    };

    return mt8668_dts_pbus_add_device(dts, &eint);
}
