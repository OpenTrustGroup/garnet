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

#include <soc/mt8676/mt8676.h>

#include "mt8676-board.h"

static zx_status_t mt8676_board_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    zxlogf(TRACE, "mt8676_board_ioctl %x\n", op);
    mt8676_board_t* board = ctx;
    const pdev_enable_info_t* info = in_buf;

    switch (op) {
    case IOCTL_NBL_BOARD_PDEV_ENABLE:
        return pbus_device_enable(&board->pbus_proto, info->vid, info->pid,
                                  info->did, info->enable);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void mt8676_board_release(void* ctx) {
    mt8676_board_t* board = ctx;
    free(board);
}

static zx_protocol_device_t mt8676_board_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = mt8676_board_ioctl,
    .release = mt8676_board_release,
};

static int mt8676_board_start_thread(void* arg) {
    mt8676_board_t* board = arg;
    zx_status_t status;

    zxlogf(INFO, "zjm ==fan==> mt8676_board_start_thread\n");
    if ((status = mt8676_board_wdt_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mtk_gpio_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_ramfb_init\n");
    if ((status = mt8676_board_ramfb_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_ramfb_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_famebuffer_init\n");
    if ((status = mt8676_board_famebuffer_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_famebuffer_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_spi_init\n");
    if ((status = mt8676_board_spi_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_spi_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_ufs_init\n");
    if ((status = mt8676_board_ufs_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_ufs_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_gpio_init\n");
    if ((status = mt8676_board_gpio_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_gpio_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_monitor_init\n");
    if ((status = mt8676_board_monitor_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_monitor_init failed: %d\n", status);
	    goto fail;
    }
    zxlogf(INFO, "mt8676_board_guest_alloc_init\n");
    if ((status = mt8676_board_guest_alloc_init(board)) != ZX_OK) {
	    zxlogf(ERROR, "mt8676_board_guest_alloc_init failed: %d\n", status);
	    goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "%s failed, not all devices have been initialized\n",
            __func__);
    return status;
}

static zx_status_t mt8676_board_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "%s INFO %d\n", __func__, __LINE__);

    mt8676_board_t* board = calloc(1, sizeof(mt8676_board_t));
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

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "nbl_board",
        .ctx = board,
        .ops = &mt8676_board_device_protocol,
        .flags = DEVICE_ADD_MUST_ISOLATE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }
    zxlogf(TRACE, "%s TRACE %d\n", __func__, __LINE__);
    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, mt8676_board_start_thread, board,
                                        "mt8676-thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }
    return ZX_OK;
    zxlogf(TRACE, "%s TRACE %d\n", __func__, __LINE__);

fail:
    zxlogf(ERROR, "%s failed %d\n", __func__, __LINE__);
    mt8676_board_release(board);
    return status;
}

static zx_driver_ops_t mt8676_board_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mt8676_board_bind,
};


ZIRCON_DRIVER_BEGIN(mt8676, mt8676_board_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MTK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8676),
ZIRCON_DRIVER_END(mt8676)

