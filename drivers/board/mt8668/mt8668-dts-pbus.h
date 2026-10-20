// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2026 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zircon/types.h>

struct mt8668_board;

typedef struct mt8668_dts_pbus {
    struct mt8668_board* board;
    void* dtb;
    size_t size;
} mt8668_dts_pbus_t;

typedef enum {
    MT8668_DTS_LOOKUP_ALIAS,
    MT8668_DTS_LOOKUP_COMPATIBLE,
} mt8668_dts_lookup_t;

typedef enum {
    MT8668_DTS_BTI_NONE,
    MT8668_DTS_BTI_DEFAULT,
    MT8668_DTS_BTI_INDEXED,
} mt8668_dts_bti_t;

typedef struct mt8668_dts_pbus_device {
    const char* name;
    const char* key;
    mt8668_dts_lookup_t lookup;
    const char* compatible;
    uint32_t required_mmios;
    uint32_t required_irqs;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    mt8668_dts_bti_t bti;
    uint32_t bti_id;
} mt8668_dts_pbus_device_t;

typedef zx_status_t (*mt8668_dts_pbus_register_t)(const mt8668_dts_pbus_t* dts);

zx_status_t mt8668_dts_pbus_open(struct mt8668_board* board, mt8668_dts_pbus_t* dts);
void mt8668_dts_pbus_close(mt8668_dts_pbus_t* dts);
zx_status_t mt8668_dts_pbus_add_device(const mt8668_dts_pbus_t* dts,
                                       const mt8668_dts_pbus_device_t* desc);
zx_status_t mt8668_dts_pbus_add_alias_prefix(const mt8668_dts_pbus_t* dts,
                                             const mt8668_dts_pbus_device_t* desc,
                                             const char* prefix);

zx_status_t mt8668_board_guest_alloc_register_from_dts(const mt8668_dts_pbus_t* dts);
zx_status_t mt8668_board_eint_register_from_dts(const mt8668_dts_pbus_t* dts);
zx_status_t mt8668_board_i2c_register_from_dts(const mt8668_dts_pbus_t* dts);
zx_status_t mt8668_board_rtc_register_from_dts(const mt8668_dts_pbus_t* dts);
zx_status_t mt8668_board_spi_register_from_dts(const mt8668_dts_pbus_t* dts);
zx_status_t mt8668_board_ufs_register_from_dts(const mt8668_dts_pbus_t* dts);
zx_status_t mt8668_board_wdt_register_from_dts(const mt8668_dts_pbus_t* dts);
