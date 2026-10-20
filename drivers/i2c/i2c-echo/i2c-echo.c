// SPDX-License-Identifier: BSD-3-Clause

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <ddk/debug.h>
#include <binding.h>
#include <string.h>

zx_status_t i2c_echo_write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
    struct i2c_client *client = (struct i2c_client *)ctx;
    char rbuf[256];

    struct i2c_msg msgs[] = {
        {
            .addr = client->addr,
            .flags = 0,
            .len = count - 1,
            .buf = (void *)buf
        },
        {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = count - 1,
            .buf = (void *)rbuf
        },
    };

    int ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret < 0) {
        zxlogf(ERROR, "i2c-echo: i2c_transfer failed with error %d\n", ret);
        return ret;
    }

    zxlogf(INFO, "i2c-echo replies '%s'\n", rbuf);
    *actual = count;
    return ZX_OK;
}

static zx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .write = i2c_echo_write,
};

static int echo_probe(struct i2c_client *client,
             const struct i2c_device_id *id) {

    zx_device_t* zxdev;
    zx_status_t status;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "echo-i2c",
        .ctx = client,
        .ops = &i2c_device_proto,
    };

    status = device_add(client->parent, &args, &zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "echo_probe: device_add failed\n");
        return -ENODEV;
    }

    zxlogf(INFO, "I2C echo registered, address=0x%x\n", client->addr);
    return 0;
}
I2C_DRIVER_ENTRY(echo_probe);

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = ddk_init,
    .bind = ddk_bind,
    .release = ddk_release,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(i2c-echo, driver_ops, "i2c-echo", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_QEMU_ECHO_I2C),
ZIRCON_DRIVER_END(i2c-echo)