// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 GoldenRiver Technologies Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

typedef int (*platform_probe_fn_t)(struct platform_device* pdev);

#define PLATFORM_DRIVER_ENTRY(fn) \
  platform_probe_fn_t __platform_device_probe = fn;

typedef int (*i2c_probe_fn_t)(struct i2c_client *, const struct i2c_device_id *);

#define I2C_DRIVER_ENTRY(fn) \
  i2c_probe_fn_t __i2c_device_probe = fn;

__BEGIN_CDECLS

zx_status_t ddk_init(void** out_ctx);

zx_status_t ddk_bind(void* ctx, zx_device_t* parent);

void ddk_release(void* ctx);

__END_CDECLS