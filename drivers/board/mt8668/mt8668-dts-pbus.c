// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2026 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8668-dts-pbus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <devicetree/devicetree.h>
#include <zircon/boot/bootdata.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "mt8668-board.h"

static const pbus_bti_t default_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static uint32_t mt8668_dts_gic_flags_to_mode(uint32_t flags) {
    switch (flags & 0xf) {
    case 1:
        return ZX_INTERRUPT_MODE_EDGE_HIGH;
    case 2:
        return ZX_INTERRUPT_MODE_EDGE_LOW;
    case 4:
        return ZX_INTERRUPT_MODE_LEVEL_HIGH;
    case 8:
        return ZX_INTERRUPT_MODE_LEVEL_LOW;
    default:
        return ZX_INTERRUPT_MODE_DEFAULT;
    }
}

zx_status_t mt8668_dts_pbus_open(struct mt8668_board* board, mt8668_dts_pbus_t* dts) {
    if (!board || !dts) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t size = 0;
    zx_status_t status = pbus_get_boot_item(&board->pbus_proto, BOOTDATA_DEVICETREE, NULL, 0,
                                            &size);
    if (status == ZX_ERR_NOT_FOUND || status == ZX_ERR_NOT_SUPPORTED) {
        zxlogf(ERROR, "mt8668 DTS: missing devicetree boot item: %d\n", status);
        return status;
    }
    if (status != ZX_ERR_BUFFER_TOO_SMALL || size == 0) {
        zxlogf(ERROR, "mt8668 DTS: invalid devicetree boot item: status=%d size=%zu\n", status,
               size);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    void* dtb = malloc(size);
    if (!dtb) {
        zxlogf(ERROR, "mt8668 DTS: could not allocate %zu-byte devicetree buffer\n", size);
        return ZX_ERR_NO_MEMORY;
    }

    size_t actual = 0;
    status = pbus_get_boot_item(&board->pbus_proto, BOOTDATA_DEVICETREE, dtb, size, &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "mt8668 DTS: could not read devicetree boot item: %d\n", status);
        free(dtb);
        return status;
    }
    if (actual != size) {
        zxlogf(ERROR, "mt8668 DTS: devicetree size changed from %zu to %zu\n", size, actual);
        free(dtb);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    status = devicetree_validate(dtb, size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "mt8668 DTS: invalid devicetree: %d\n", status);
        free(dtb);
        return status;
    }

    dts->board = board;
    dts->dtb = dtb;
    dts->size = size;
    return ZX_OK;
}

void mt8668_dts_pbus_close(mt8668_dts_pbus_t* dts) {
    if (!dts) {
        return;
    }
    free(dts->dtb);
    dts->board = NULL;
    dts->dtb = NULL;
    dts->size = 0;
}

static zx_status_t mt8668_dts_query_node(const mt8668_dts_pbus_t* dts,
                                         const mt8668_dts_pbus_device_t* desc,
                                         devicetree_node_t* out_node) {
    switch (desc->lookup) {
    case MT8668_DTS_LOOKUP_ALIAS:
        return devicetree_query_node_by_alias(dts->dtb, dts->size, desc->key, out_node);
    case MT8668_DTS_LOOKUP_COMPATIBLE:
        return devicetree_query_node_by_compatible(dts->dtb, dts->size, desc->key, out_node);
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_status_t mt8668_dts_node_to_pbus(const mt8668_dts_pbus_device_t* desc,
                                           const devicetree_node_t* node,
                                           pbus_mmio_t* mmios, pbus_irq_t* irqs) {
    if (!devicetree_node_has_compatible(node, desc->compatible)) {
        zxlogf(ERROR, "%s: node %s is not compatible with %s\n", desc->name, node->path,
               desc->compatible);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (node->reg_count != desc->required_mmios) {
        zxlogf(ERROR, "%s: expected %u MMIOs, got %u\n", desc->name, desc->required_mmios,
               node->reg_count);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (node->interrupt_count != desc->required_irqs) {
        zxlogf(ERROR, "%s: expected %u IRQs, got %u\n", desc->name, desc->required_irqs,
               node->interrupt_count);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    for (uint32_t i = 0; i < node->reg_count; i++) {
        if (node->regs[i].size > SIZE_MAX) {
            zxlogf(ERROR, "%s: reg[%u] size too large 0x%llx\n", desc->name, i,
                   (unsigned long long)node->regs[i].size);
            return ZX_ERR_OUT_OF_RANGE;
        }
        mmios[i].base = (zx_paddr_t)node->regs[i].addr;
        mmios[i].length = (size_t)node->regs[i].size;
    }

    for (uint32_t i = 0; i < node->interrupt_count; i++) {
        uint32_t irq = devicetree_gic_irq(&node->interrupts[i]);
        if (irq == UINT32_MAX) {
            zxlogf(ERROR, "%s: unsupported interrupt encoding\n", desc->name);
            return ZX_ERR_NOT_SUPPORTED;
        }
        irqs[i].irq = irq;
        irqs[i].mode = mt8668_dts_gic_flags_to_mode(
            devicetree_gic_flags(&node->interrupts[i]));
    }

    return ZX_OK;
}

zx_status_t mt8668_dts_pbus_add_device(const mt8668_dts_pbus_t* dts,
                                       const mt8668_dts_pbus_device_t* desc) {
    if (!dts || !dts->board || !dts->dtb || !desc) {
        return ZX_ERR_INVALID_ARGS;
    }

    devicetree_node_t node;
    zx_status_t status = mt8668_dts_query_node(dts, desc, &node);
    if (status == ZX_ERR_UNAVAILABLE) {
        if (desc->lookup == MT8668_DTS_LOOKUP_ALIAS &&
            !devicetree_node_has_compatible(&node, desc->compatible)) {
            zxlogf(ERROR, "%s: node %s is not compatible with %s\n", desc->name, node.path,
                   desc->compatible);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        zxlogf(INFO, "%s: disabled by DTS\n", desc->name);
        return ZX_OK;
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: devicetree query failed: %d\n", desc->name, status);
        return status;
    }

    pbus_mmio_t mmios[DEVICETREE_MAX_REGS] = {};
    pbus_irq_t irqs[DEVICETREE_MAX_INTERRUPTS] = {};
    status = mt8668_dts_node_to_pbus(desc, &node, mmios, irqs);
    if (status != ZX_OK) {
        return status;
    }

    pbus_bti_t indexed_bti = {
        .iommu_index = 0,
        .bti_id = desc->bti_id,
    };
    const pbus_bti_t* btis = NULL;
    uint32_t bti_count = 0;
    if (desc->bti == MT8668_DTS_BTI_DEFAULT) {
        btis = default_btis;
        bti_count = countof(default_btis);
    } else if (desc->bti == MT8668_DTS_BTI_INDEXED) {
        btis = &indexed_bti;
        bti_count = 1;
    }

    pbus_dev_t dev = {
        .name = desc->name,
        .vid = desc->vid,
        .pid = desc->pid,
        .did = desc->did,
        .mmios = mmios,
        .mmio_count = node.reg_count,
        .irqs = irqs,
        .irq_count = node.interrupt_count,
        .btis = btis,
        .bti_count = bti_count,
    };

    status = pbus_device_add(&dts->board->pbus_proto, &dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed: %d\n", desc->name, status);
    }
    return status;
}

zx_status_t mt8668_dts_pbus_add_alias_prefix(const mt8668_dts_pbus_t* dts,
                                             const mt8668_dts_pbus_device_t* desc,
                                             const char* prefix) {
    if (!dts || !desc || !prefix) {
        return ZX_ERR_INVALID_ARGS;
    }

    for (uint32_t i = 0;; i++) {
        char name[16];
        int len = snprintf(name, sizeof(name), "%s%u", prefix, i);
        if (len < 0 || (size_t)len >= sizeof(name)) {
            return ZX_ERR_OUT_OF_RANGE;
        }

        mt8668_dts_pbus_device_t alias_desc = *desc;
        alias_desc.name = name;
        alias_desc.key = name;
        alias_desc.lookup = MT8668_DTS_LOOKUP_ALIAS;
        if (alias_desc.bti == MT8668_DTS_BTI_INDEXED) {
            alias_desc.bti_id = i;
        }

        zx_status_t status = mt8668_dts_pbus_add_device(dts, &alias_desc);
        if (status == ZX_ERR_NOT_FOUND) {
            return i == 0 ? status : ZX_OK;
        }
        if (status != ZX_OK) {
            return status;
        }
    }
}
