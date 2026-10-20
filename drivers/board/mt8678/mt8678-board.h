// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
//#include <ddk/protocol/ufs.h>
#include "ufs_protocol.h"
#include "wdt.h"

typedef struct {
    platform_bus_protocol_t pbus_proto;
    zx_device_t* parent;
    // device drivers
    ufs_protocol_t ufs;
    wdt_protocol_t wdt;
} mt8678_board_t;

zx_status_t mt8678_board_ufs_init(mt8678_board_t* board);
zx_status_t mt8678_board_wdt_init(mt8678_board_t* board);
