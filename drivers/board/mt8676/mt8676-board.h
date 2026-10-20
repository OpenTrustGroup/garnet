// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include "wdt.h"

typedef struct {
    platform_bus_protocol_t pbus_proto;
    zx_device_t* parent;
    // device drivers
    wdt_protocol_t wdt;
} mt8676_board_t;

zx_status_t mt8676_board_wdt_init(mt8676_board_t* board);
zx_status_t mt8676_board_ramfb_init(mt8676_board_t* board);
zx_status_t mt8676_board_famebuffer_init(mt8676_board_t* board);
zx_status_t mt8676_board_spi_init(mt8676_board_t* board);
zx_status_t mt8676_board_ufs_init(mt8676_board_t* board);
zx_status_t mt8676_board_gpio_init(mt8676_board_t* board);
zx_status_t mt8676_board_monitor_init(mt8676_board_t* board);
zx_status_t mt8676_board_guest_alloc_init(mt8676_board_t* board);
