// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/debug.h>

#include <zircon/types.h>
#include <zircon/assert.h>

#include <soc/mt8676/mt8676-hw.h>

#include "mt8676-board.h"

// NOTE:
// mphy is already initialized in preloader stage and no need
// to change any setting of mphy, neither the PWR/CRG.
static const pbus_mmio_t ramfb_mmios[] = {
    // register
    {
        .base = DISP_OVLSYS0_MUTEX_BASE,
        .length = DISP_OVLSYS0_MUTEX_LEN,
    },
};

static const pbus_irq_t ramfb_irqs[] = {
    {
        .irq = DISP_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t ramfb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static const pbus_dev_t ramfb_dev = {
    .name = "ramfb",
    .vid = PDEV_VID_MTK,
    .pid = PDEV_PID_MT8676,
    .did = PDEV_DID_GRT_RAMFB,
    .mmios = ramfb_mmios,
    .mmio_count = countof(ramfb_mmios),
    .irqs = ramfb_irqs,
    .irq_count = countof(ramfb_irqs),
    .btis = ramfb_btis,
    .bti_count = countof(ramfb_btis),
};

zx_status_t mt8676_board_ramfb_init(mt8676_board_t* board) {
    // register to platform bus
    zx_status_t status = pbus_device_add(&board->pbus_proto, &ramfb_dev,
            PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed: %d\n",
                __func__, status);
        return status;
    }
    return ZX_OK;
}

