// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/pci.h>
#include <hw/reg.h>
#include <threads.h>
#include <assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "monitor.h"
#include <zircon/device/grt-monitor.h>
#include <ddk/io-buffer.h>
#include <fcntl.h>
// #include <trusty_std.h>
#include <ctype.h>

typedef struct monitor_device {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    zx_handle_t bti;
    mtx_t lock;
    uint32_t virq;
    zx_paddr_t paddr;
    zx_vaddr_t vaddr;
    zx_handle_t event;
    bool timer_enabled;
    zx_time_t last_heartbeat_time;
} monitor_device_t;

// implement device protocol

static void monitor_unbind(void* ctx) {
    monitor_device_t* monitor = ctx;
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    device_remove(monitor->zxdev);
}

static void monitor_release(void* ctx) {
    monitor_device_t* monitor = ctx;

    free(monitor);
}

static int timer_thread(void* arg) {
    monitor_device_t* dev = (monitor_device_t*)arg;
    const zx_duration_t interval = ZX_SEC(TICK_TIME);
    const zx_duration_t timeout = ZX_SEC(MAX_TIME_OUT);
    zx_time_t deadline = zx_clock_get(ZX_CLOCK_MONOTONIC) + interval;
    bool is_timeout = false;
    static uint64_t cnt = 0;

    while (1) {
        zx_status_t status = zx_nanosleep(deadline);
        if (status != ZX_OK && status != ZX_ERR_INTERNAL_INTR_RETRY) {
            zxlogf(ERROR, "Sleep failed: %d\n", status);
            break;
        }
        deadline += interval;

        zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);

        bool enabled = dev->timer_enabled;
        zx_time_t last = dev->last_heartbeat_time;

        if (!enabled) {
            continue;
        }

        zxlogf(INFO, "currnet cnt:%lu , %lu\n", cnt++, (now - last));
        if ((int64_t)(now - last) > timeout) {
            if (!is_timeout) {
                zxlogf(ERROR, "Timeout detected! Sending send_data.\n");
                is_timeout = true;
                status = zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
                if (status != ZX_OK) {
                    zxlogf(ERROR, "signal0 failed: %d\n", status);
                }
            }
        } else {
            if (is_timeout) {
                zxlogf(INFO, "Timeout cleared! Sending send_data.\n");
                is_timeout = false;
                status = zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_1);
                if (status != ZX_OK) {
                    zxlogf(ERROR, "signal1 failed: %d\n", status);
                }
            }
        }
    }

    return 0;
}

void start_timer_thread(monitor_device_t* dev) {
    thrd_t thread;
    if (thrd_create(&thread, timer_thread, dev) != thrd_success) {
        zxlogf(ERROR, "Failed to create timer thread\n");
        return;
    }
    thrd_detach(thread);
}

#if 0
zx_status_t recv_data(monitor_device_t *dev, unsigned long pa, unsigned long size) {
    zx_handle_t vmo;
    zx_vaddr_t vaddr;
    zx_vaddr_t out_vaddr;
    zx_status_t st;
    zx_paddr_t vmo_base = ROUNDDOWN(pa, PAGE_SIZE);
    size_t vmo_size = ROUNDUP(size, PAGE_SIZE);

    if (size > MAX_SEND_DATA_SIZE) {
        zxlogf(ERROR, "Exceed the maximum transmission size.\n");
        return ZX_ERR_INVALID_ARGS;
    }

    st = zx_vmo_create_physical(get_root_resource(), vmo_base, vmo_size, &vmo);
    if (st != ZX_OK) {
        printf("zx_vmo_create_physical fail:%d\n", st);
        return st;
    }

    st = zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED);
    if (st != ZX_OK) {
        printf("zx_vmo_set_cache_policy fail:%d\n", st);
        zx_handle_close(vmo);
        return st;
    }

    st = zx_vmar_map(zx_vmar_root_self(),
                     ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                     0, vmo, 0, size,
                     &vaddr);
    if (st != ZX_OK) {
        printf("zx_vmar_map fail:%d\n", st);
        zx_handle_close(vmo);
        return st;
    } else {
        out_vaddr = (vaddr + (pa - vmo_base));
    }

    char *dat =  (char *)out_vaddr;

    zx_vmar_unmap(zx_vmar_root_self(), vmo, size);
    zx_handle_close(vmo);
    return ZX_OK;
}
#endif

static zx_status_t monitor_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    monitor_device_t* dev = ctx;
    zx_status_t status;

    switch (op) {
        case IOCTL_GRT_MONITOR_SEND_DATA: {
            // recv_data(dev, ((grt_monitor_ioctl_data_t *)in_buf)->phy_addr, ((grt_monitor_ioctl_data_t *)in_buf)->size);
            return ZX_OK;
        }
        case IOCTL_GRT_GET_MONITOR_EVENT: {
            if (out_len != sizeof(zx_handle_t)) {
                zxlogf(ERROR, "ZX_ERR_BUFFER_TOO_SMALL\n");
                return ZX_ERR_BUFFER_TOO_SMALL;
            }
            zx_handle_t* out = (zx_handle_t*) out_buf;
            if ((status = zx_handle_duplicate(dev->event, ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL, out)) < 0) {
                zxlogf(ERROR, "zx_handle_duplicate :%d\n", status);
                return status;
            } else {
                *out_actual = sizeof(zx_handle_t);
                zx_info_handle_basic_t info;
                zx_object_get_info(*out, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
                zxlogf(TRACE, "monitor event koid=%lu\n", info.koid);
                return ZX_OK;
            }
        }
        case IOCTL_GRT_ENABLE_MONITOR: {
            if (in_len < sizeof(bool)) {
                return ZX_ERR_INVALID_ARGS;
            }
            mtx_lock(&dev->lock);
            dev->timer_enabled = *(bool *)in_buf;
            dev->last_heartbeat_time = zx_clock_get(ZX_CLOCK_MONOTONIC);
            mtx_unlock(&dev->lock);
            zxlogf(INFO, "timer_enabled :%d\n",  dev->timer_enabled);

            return ZX_OK;
        }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_protocol_device_t monitor_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = monitor_unbind,
    .ioctl = monitor_ioctl,
    .release = monitor_release,
};

// implement driver object:

static zx_status_t monitor_bind(void* ctx, zx_device_t* dev) {
    zx_status_t status;

    // map resources and initialize the device
    monitor_device_t* device = calloc(1, sizeof(monitor_device_t));
    if (!device)
        return ZX_ERR_NO_MEMORY;

    if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV,
            (void*) &device->pdev))
        return ZX_ERR_NOT_SUPPORTED;

    status = pdev_get_bti(&device->pdev, 0, &device->bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "monitor: could not get BTI handle: %d\n", status);
        goto fail;
    }

    // create and add the monitor (char) device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "mtk_monitor",
        .ctx = device,
        .ops = &monitor_device_proto,
        .flags = DEVICE_ADD_MUST_ISOLATE,
    };

    status = device_add(dev, &args, &device->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    if ((status = zx_event_create(0, &device->event)) < 0) {
        zxlogf(ERROR, "cannot create monitor event: %d\n", status);
        goto fail;
    }

    mtx_init(&device->lock, mtx_plain);
    device->last_heartbeat_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

    device->virq = MONITOR_VIRQ;
    device->paddr = 0;
    device->vaddr = 0;
    device->timer_enabled = 0;

    start_timer_thread(device);

    zxlogf(INFO, "initialized nebula cluster monitor\n");

    return ZX_OK;

fail:
    monitor_release(device);
    return status;
}

static zx_driver_ops_t monitor_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = monitor_bind,
};

ZIRCON_DRIVER_BEGIN(monitor, monitor_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8676),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_MONITOR),
ZIRCON_DRIVER_END(monitor)