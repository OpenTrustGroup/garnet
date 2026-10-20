// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/device/block.h>
#include <zircon/types.h>
#include <threads.h>
#include "supervision.h"

#define MAX_PCPU    8


typedef struct mtk_wdt {
    platform_bus_protocol_t pbus;
    platform_device_protocol_t pdev;
    pdev_device_info_t dev_info;
    zx_device_t* zxdev;
    void* wdt_base;
    zx_handle_t vmo_handle;
    uint32_t timeout;
	uint32_t pretimeout;
    uint64_t status;

    wdt_protocol_t wdt_proto;
    mtx_t lock;
    uint32_t cpu_id;
    thrd_t worker_thread[MAX_PCPU];
    bool worker_thread_running[MAX_PCPU];
    bool kick_bits[MAX_PCPU];
    uint64_t last_kick_time[MAX_PCPU];
    uint64_t nebula_ping_count;
    uint64_t nebula_kick_time;
    uint64_t wdt_last_updata_time;
    uint64_t sos_hartbit_time;
    uint64_t guest_manager_heartbeat_count;
    zx_time_t guest_manager_last_heartbeat;
    zx_time_t guest_manager_config_time;
    bool bsuspend;
    bool bstop_sos;
    wdt_supervision_t supervision;
    bool guest_manager_watchdog_enabled;
    bool guest_manager_heartbeat_armed;
    bool guest_manager_heartbeat_lost_logged;
    uint32_t guest_manager_heartbeat_interval_sec;
    uint32_t guest_manager_heartbeat_miss_threshold;
    uint32_t guest_manager_watchdog_boot_grace_sec;
} mtk_wdt_t;
