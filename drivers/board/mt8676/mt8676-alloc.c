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

static pbus_dev_t guest_alloc_dev = {
    .name = "guest_allocator",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_GRT_GUEST_MEMORY_ALLOCATOR,
};

zx_status_t mt8676_board_guest_alloc_init(mt8676_board_t* board) {
    // register to platform bus
    zx_status_t status = pbus_device_add(&board->pbus_proto, &guest_alloc_dev,
            PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed: %d\n",
                __func__, status);
        return status;
    }
    return ZX_OK;
}

