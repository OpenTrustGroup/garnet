// SPDX-License-Identifier: BSD-3-Clause


// Copyright 2025 GoldenRiver Technologies Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <binding.h>

#include "lib/nebula/base/logging.h"
#include "garnet/lib/linux/platform_device.h"
#include "garnet/lib/linux/i2c_device.h"

class DeviceContext {
 public:
  zx_device_t *parent;
};

zx_status_t ddk_init(void** out_ctx) {
  *out_ctx = new DeviceContext();
  if (!*out_ctx) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t ddk_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  DeviceContext* dev_ctx = (DeviceContext*)ctx;

  dev_ctx->parent = parent;
  status = PlatformDeviceContext::Create(parent);
  if (ZX_ERR_NOT_FOUND == status) {
    //LOG(ERROR) << "Platform device probe failed with error: " << status;
    //LOG(INFO) << "I2cClientContext Create";
    status = I2cDeviceContext::Create(parent);
  }
  return status;
}

void ddk_release(void* ctx) {
  DeviceContext* dev_ctx = (DeviceContext*)ctx;
  delete dev_ctx;
}