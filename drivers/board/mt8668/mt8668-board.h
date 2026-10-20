// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <zircon/types.h>

typedef struct mt8668_board {
    platform_bus_protocol_t pbus_proto;
    zx_device_t* parent;
} mt8668_board_t;
