// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

extern zx_status_t wlan_bind(void* ctx, zx_device_t* device);

static zx_driver_ops_t wlan_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = wlan_bind,
};

// clang-format: off
ZIRCON_DRIVER_BEGIN(wlan, wlan_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_WLANMAC),
ZIRCON_DRIVER_END(wlan)
