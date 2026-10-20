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
static const pbus_mmio_t spi_mmios[] = {
    // SPI0
    {
        .base = SPI0_MMIO_BASE,
        .length = SPI0_MMIO_LEN,
    },
    // SPI1
    {
        .base = SPI1_MMIO_BASE,
        .length = SPI1_MMIO_LEN,
    },
    // SPI2
    {
        .base = SPI2_MMIO_BASE,
        .length = SPI2_MMIO_LEN,
    },
    // SPI3
    {
        .base = SPI3_MMIO_BASE,
        .length = SPI3_MMIO_LEN,
    },
    // SPI4
    {
        .base = SPI4_MMIO_BASE,
        .length = SPI4_MMIO_LEN,
    },
    // SPI5
    {
        .base = SPI5_MMIO_BASE,
        .length = SPI5_MMIO_LEN,
    },
    // SPI6
    {
        .base = SPI6_MMIO_BASE,
        .length = SPI6_MMIO_LEN,
    },
    // SPI7
    {
        .base = SPI7_MMIO_BASE,
        .length = SPI7_MMIO_LEN,
    },
};

static const pbus_irq_t spi_irqs[] = {
    // SPI0
    {
        .irq = SPI0_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI1
    {
        .irq = SPI1_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI2
    {
        .irq = SPI2_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI3
    {
        .irq = SPI3_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI4
    {
        .irq = SPI4_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI5
    {
        .irq = SPI5_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI6
    {
        .irq = SPI6_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    // SPI7
    {
        .irq = SPI7_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t spi_btis[] = {
    // SPI0
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
    // SPI1
    {
        .iommu_index = 0,
        .bti_id = 1,
    },
    // SPI2
    {
        .iommu_index = 0,
        .bti_id = 2,
    },
    // SPI3
    {
        .iommu_index = 0,
        .bti_id = 3,
    },
    // SPI4
    {
        .iommu_index = 0,
        .bti_id = 4,
    },
    // SPI5
    {
        .iommu_index = 0,
        .bti_id = 5,
    },
    // SPI6
    {
        .iommu_index = 0,
        .bti_id = 6,
    },
    // SPI7
    {
        .iommu_index = 0,
        .bti_id = 7,
    },
};

static const pbus_dev_t spi_dev = {
    .name = "spi",
    .vid = PDEV_VID_MTK,
    .pid = PDEV_PID_MT8676,
    .did = PDEV_DID_GRT_SPI,
    .mmios = spi_mmios,
    .mmio_count = countof(spi_mmios),
    .irqs = spi_irqs,
    .irq_count = countof(spi_irqs),
    .btis = spi_btis,
    .bti_count = countof(spi_btis),
};

zx_status_t mt8676_board_spi_init(mt8676_board_t* board) {
    zx_status_t status = ZX_OK;
    int spi_num = countof(spi_mmios);
    char spi_names[spi_num][16];
    // Initialize SPI devices for multi bus
    for (int i = 0; i < spi_num; i++) {
        pbus_dev_t spi_dev_inst = spi_dev;
        spi_dev_inst.mmios = &spi_mmios[i];
        spi_dev_inst.mmio_count = 1;
        spi_dev_inst.irqs = &spi_irqs[i];
        spi_dev_inst.irq_count = 1;
        spi_dev_inst.btis = &spi_btis[i];
        spi_dev_inst.bti_count = 1;
        snprintf(spi_names[i], sizeof(spi_names[i]), "spi%d", i);
        spi_dev_inst.name = spi_names[i];

        status = pbus_device_add(&board->pbus_proto, &spi_dev_inst, PDEV_ADD_PBUS_DEVHOST);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: pbus_device_add failed for bus %d: %d\n", __func__, i, status);
            return status;
        }
    }
    return ZX_OK;
}
