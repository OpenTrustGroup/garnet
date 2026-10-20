// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <nebula/device/board.h>

#include <soc/mt8678/mt8678.h>

#include "mt8678-board.h"

static zx_status_t mt8678_board_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    zxlogf(TRACE, "mt8678_board_ioctl %x\n", op);
    mt8678_board_t* board = ctx;
    const pdev_enable_info_t* info = in_buf;

    switch (op) {
    case IOCTL_NBL_BOARD_PDEV_ENABLE:
        return pbus_device_enable(&board->pbus_proto, info->vid, info->pid,
                                  info->did, info->enable);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void mt8678_board_release(void* ctx) {
    mt8678_board_t* board = ctx;
    free(board);
}

static zx_protocol_device_t mt8678_board_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = mt8678_board_ioctl,
    .release = mt8678_board_release,
};

static int mt8678_board_start_thread(void* arg) {
    mt8678_board_t* board = arg;
    zx_status_t status;

    if ((status = mt8678_board_wdt_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mtk_gpio_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(ERROR, "=fan==> mt8678_board_start_thread: %d\n", status);
    if ((status = mt8678_board_ufs_init(board)) != ZX_OK) {
        zxlogf(ERROR, "mtk_gpio_init failed: %d\n", status);
        goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "%s failed, not all devices have been initialized\n",
            __func__);
    return status;
}

static zx_status_t mt8678_board_bind(void* ctx, zx_device_t* parent) {
    mt8678_board_t* board = calloc(1, sizeof(mt8678_board_t));
    if (!board) {
        return ZX_ERR_NO_MEMORY;
    }
    board->parent = parent;

    // grab the platform bus protocol, then we can probe device on the bus.
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS,
                                             &board->pbus_proto);
    if (status != ZX_OK) {
        goto fail;
    }

    board->ufs.ctx = board;
#if 0
    mt8678_soc_t* soc;
    status = mt8678_soc_init(&soc);
    if (status != ZX_OK) {
        goto fail;
    }
#endif
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "nbl_board",
        .ctx = board,
        .ops = &mt8678_board_device_protocol,
        .flags = DEVICE_ADD_MUST_ISOLATE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto soc_release;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, mt8678_board_start_thread, board,
                                        "mt8678-thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        //goto soc_release;
    }
    return ZX_OK;

soc_release:
    //mt8678_soc_release(soc);
fail:
    zxlogf(ERROR, "%s failed %d\n", __func__, status);
    mt8678_board_release(board);
    return status;
}

static zx_driver_ops_t mt8678_board_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mt8678_board_bind,
};


ZIRCON_DRIVER_BEGIN(mt8678, mt8678_board_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8678),
ZIRCON_DRIVER_END(mt8678)

