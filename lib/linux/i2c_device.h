/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/i2c.h>

#include <linux/platform_device.h>
#include <linux/i2c.h>

#include "lib/nebula/base/logging.h"
#include "garnet/lib/linux/singleton.h"

#include <vector>

class I2cDeviceContext : public Singleton<I2cDeviceContext> {
 public:
  static zx_status_t Create(zx_device_t* parent);
  struct i2c_client* get_linux_i2c_dev() { return &linux_i2c_dev_; }
  struct i2c_adapter* get_linux_i2c_adap() { return &linux_i2c_adap_; }
  struct i2c_device_id* get_linux_i2c_dev_id() { return &linux_i2c_dev_id_; }
  uint16_t get_i2c_dev_addr() { return i2c_channel.address; }
  zx_device_t* parent_device() { return parent_; }

 private:
  void Init(zx_device_t* parent) {
    parent_ = parent;

    auto status =
        device_get_protocol(parent_, ZX_PROTOCOL_PLATFORM_DEV, (void*)&pdev_);
    CHECK(status == ZX_OK) << "ZX_PROTOCOL_PLATFORM_DEV not available";

    status = pdev_get_device_info(&pdev_, &dev_info_);
    CHECK(status == ZX_OK) << "pdev_get_device_info failed";

    status = pdev_get_i2c_channels_info(&pdev_, 0, &i2c_channel);
    CHECK(status == ZX_OK) << "pdev_get_i2c_channels_info failed";
  }
  zx_device_t* parent_;
  platform_device_protocol_t pdev_;
  pdev_device_info_t dev_info_;
  pdev_i2c_channel_t i2c_channel;

  struct i2c_client linux_i2c_dev_ = {};
  struct i2c_adapter linux_i2c_adap_ = {};
  struct i2c_device_id linux_i2c_dev_id_ = {};
};
