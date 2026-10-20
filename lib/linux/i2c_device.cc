/* SPDX-License-Identifier: GPL-2.0 */

#include "garnet/lib/linux/platform_device.h"
#include "garnet/lib/linux/i2c_device.h"

#include <binding.h>
#include <linux/platform_device.h>
#include <zircon/types.h>

__WEAK i2c_probe_fn_t __i2c_device_probe;

zx_status_t I2cDeviceContext::Create(zx_device_t* parent) {
  struct i2c_client *client;
  struct i2c_device_id *id;
  if (!__i2c_device_probe) {
    LOG(ERROR) << "__i2c_device_probe not define ";
    return ZX_ERR_NOT_FOUND;
  }

  auto I2C_ctx = I2cDeviceContext::Instance();
  if (!I2C_ctx)
    return ZX_ERR_NO_MEMORY;

  I2C_ctx->Init(parent);

  client = I2C_ctx->get_linux_i2c_dev();
  client->addr = I2C_ctx->get_i2c_dev_addr();
  strlcpy(client->name, device_get_name(parent), sizeof(client->name));
  client->parent = I2C_ctx->parent_device();
  client->adapter = I2C_ctx->get_linux_i2c_adap();
  id = I2C_ctx->get_linux_i2c_dev_id();

  int ret = __i2c_device_probe(client, id);
  if (ret < 0) {
    LOG(ERROR) << "I2c client probe failed with error: " << ret;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}
