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
#include "gpio.h"
#include <zircon/device/grt-gpio.h>
// #include <trusty_std.h>
#define MONITOR_VIRQ (956)

typedef struct gpio_device {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    void* regs;
    void* io_base;
    void* eint_e_base;
    void* gpio_din_regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;
    zx_handle_t bti;
    mtx_t lock;
    thrd_t irq_thread;
    zx_handle_t irq_handle;
    uint32_t irq;
    pdev_irq_t out_irq;
    uint32_t enable_irq;
    zx_handle_t event;
   uint32_t port;
   uint32_t virq;
} gpio_device_t;


/******************************************************************************
 * MACRO Definition
 ******************************************************************************/
/* IO_BASE:0xffff000010000000 GPIO_BASE:0xffff000010005000 EINT_E_BASE:0xffff000011ce0000 */
#define IRQ_ENABLE             1
#define IRQ_DISABLE            0
#define EINT_INDEX             12
#define DEINT_IRQ              345
#define MAX_GPIO_REG_BITS      32
#define IO_BASE                0x10000000
#define IO_SIZE                0x02000000

#define GPIO_DIN_OFFSET        (0x5000 + 0x200)
#define EINT_E_OFFSET          0x01CE0000

// GPIO_DIN_BASE          = GPIO_BASE + 0x200
// EINT_E_BASE            = IO_BASE + 0x01CE0000

// implement device protocol

static zx_status_t gpio_irq_init(gpio_device_t *dev);
static zx_status_t gpio_disable_irq(gpio_device_t *dev);

static void gpio_unbind(void* ctx) {
    gpio_device_t* gpio = ctx;
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    device_remove(gpio->zxdev);
}

static void gpio_release(void* ctx) {
    gpio_device_t* gpio = ctx;

    if (gpio->eint_e_base) {
        zx_handle_close(gpio->regs_handle);
        gpio->regs_handle = -1;
    }

    zx_handle_close(gpio->event);

    free(gpio);
}

uint32_t mt_get_gpio_in(gpio_device_t *dev, enum GPIO_PIN pin)
{
    uint32_t pos;
    uint32_t bit;
    uint32_t reg;

    if ((pin >= MT_GPIO_BASE_MAX) || (pin <= GPIO_UNSUPPORTED))
        return ZX_ERR_INVALID_ARGS;

    pos = pin / MAX_GPIO_REG_BITS;
    bit = pin % MAX_GPIO_REG_BITS;

    /* To reduce memory usage, we don't use DIN_addr[] array*/
    reg = readl(dev->gpio_din_regs + pos * 0x10);
    return (((reg & (1UL << bit)) != 0) ? 1 : 0);
}

static zx_status_t gpio_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    gpio_device_t* gpio = ctx;
    zx_status_t status;

    switch (op) {
    case IOCTL_GRT_GET_GPIO_LEVEL: {
        uint32_t pin = *(uint32_t *)in_buf;
        uint32_t level;

        level = mt_get_gpio_in(gpio, pin);
        *(uint32_t *)out_buf = level;
        *(size_t *)out_actual = sizeof(level);

        zxlogf(TRACE, "gpio: gpio_ioctl pin:%d level:%u\n", pin, level);
        return ZX_OK;
    }
    /* Unregister the interrupt function after entering the Yocto system */
    case IOCTL_GRT_RELEASE_GPIO: {
        gpio_disable_irq(gpio);
        device_remove(gpio->zxdev);
        zxlogf(INFO, "gpio: Yocto has started disable interrupts. irq status:%d\n", gpio->enable_irq);
        return ZX_OK;
    }
    case IOCTL_GRT_GPIO_GET_EVENT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t* out = (zx_handle_t*) out_buf;
        if ((status = zx_handle_duplicate(gpio->event, ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL, out)) < 0) {
            return status;
        } else {
            *out_actual = sizeof(zx_handle_t);
            return ZX_OK;
        }
        return ZX_OK;
    }
    case IOCTL_GRT_GPIO_ENABLE_IRQ: {
        uint32_t enable = *(uint32_t *)in_buf;

        if (IRQ_ENABLE == enable) {
            status = gpio_irq_init(gpio);
            if (status != ZX_OK) {
                zxlogf(ERROR, "gpio: irq enable fail %d.\n", status);
                return status;
            }
            zxlogf(INFO, "gpio: enable irq success!.\n");
        } else if (IRQ_DISABLE == enable) {
            status = gpio_disable_irq(gpio);
            if (status != ZX_OK) {
                zxlogf(ERROR, "gpio: irq disable fail %d.\n", status);
                return status;
            }
            zxlogf(INFO, "gpio: disable irq success!.\n");
        } else {
            return ZX_ERR_INVALID_ARGS;
        }

        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_protocol_device_t gpio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = gpio_unbind,
    .ioctl = gpio_ioctl,
    .release = gpio_release,
};

/* EINT_INDEX is the register offset for GPIO 65 in the EINT module */
void gpio_irq_handler(gpio_device_t* dev) {
    zx_status_t status;

    write32(dev->eint_e_base + 0x040 + dev->port * 4, 1U << (EINT_INDEX % 32));
    zxlogf(TRACE, "gpio: gpio_irq_handler IRQ:%d.\n", dev->irq);

    status = zx_object_signal(dev->event, 0, ZX_USER_SIGNAL_0);
    if (status != ZX_OK) {
        zxlogf(ERROR,"zx_object_signal failed: %d\n", status);
    }
}

static zx_status_t gpio_irq_thread(void* arg) {
    zx_status_t wait_ret;
    gpio_device_t* dev = (gpio_device_t*)arg;

    while (true) {
        uint64_t slots;
        wait_ret = zx_interrupt_wait(dev->irq_handle, &slots);
        if (wait_ret != ZX_OK) {
                zxlogf(ERROR, "gpio: ======zx_interrupt_wait failed=====>wait_ret:%d\n", wait_ret);
            break;
        }
        // handle irq
        gpio_irq_handler(dev);
    }
    return ZX_OK;
}

static zx_status_t gpio_irq_init(gpio_device_t *dev) {
    zx_status_t status;

    if (IRQ_ENABLE == dev->enable_irq) {
        zxlogf(INFO, "gpio: irq has been enable.\n");
        return ZX_OK;
    }

    status = pdev_map_interrupt(&dev->pdev, 0, &dev->irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gpio: pdev_map_interrupt failed\n");
        return status;
    }

    pdev_get_irq_info(&dev->pdev, 0, &dev->out_irq);
    dev->irq = dev->out_irq.irq;
    dev->port = EINT_INDEX / 32;
    zxlogf(INFO, "mt8676 gpio interrupt:%u\n", dev->irq);

    zx_interrupt_unmask(dev->irq_handle, dev->irq);
    status = thrd_create_with_name(&dev->irq_thread, gpio_irq_thread, dev,
                                   "gpio-irq");
    if (status != thrd_success) {
        zxlogf(ERROR, "gpio: failed to create irq thread\n");
        zx_handle_close(dev->irq_handle);
        return status;
    }
    thrd_detach(dev->irq_thread);
    dev->enable_irq  = IRQ_ENABLE;

    return ZX_OK;
}

static zx_status_t gpio_disable_irq(gpio_device_t *dev) {
    zx_status_t status;

    if (IRQ_ENABLE == dev->enable_irq) {
        status = zx_interrupt_mask(dev->irq_handle, dev->irq);
        if (status != ZX_OK) {
            zxlogf(ERROR, "gpio: mask irq failed status:%d\n", status);
        }

        status = zx_handle_close(dev->irq_handle);
        if (status != ZX_OK) {
            zxlogf(ERROR, "gpio: disable irq failed status:%d\n", status);
        }
        dev->enable_irq  = IRQ_DISABLE;
    } else {
        zxlogf(INFO, "gpio: irq has been disable.\n");
    }

    return ZX_OK;
}

// implement driver object:

static zx_status_t gpio_bind(void* ctx, zx_device_t* dev) {
    zx_status_t status;

    // map resources and initialize the device
    gpio_device_t* device = calloc(1, sizeof(gpio_device_t));
    if (!device)
        return ZX_ERR_NO_MEMORY;

    if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV,
            (void*) &device->pdev))
        return ZX_ERR_NOT_SUPPORTED;

    status = pdev_get_bti(&device->pdev, 0, &device->bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gpio: could not get BTI handle: %d\n", status);
        goto fail;
    }

    // map register window
    status = pdev_map_mmio(&device->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->regs, &device->regs_size,
                              &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gpio: failed to map firmware config: %d\n", status);
        goto fail;
    }

    // status = gpio_irq_init(device);
    // if (status != ZX_OK) {
    //     zxlogf(ERROR, "gpio: init interrupt error: %d\n", status);
    //     goto fail;
    // }

    // create and add the gpio (char) device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "mtk_gpio",
        .ctx = device,
        .ops = &gpio_device_proto,
        .flags = DEVICE_ADD_MUST_ISOLATE,
    };

    status = device_add(dev, &args, &device->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    if ((status = zx_event_create(0, &device->event)) < 0) {
        goto fail;
    }

    io_buffer_t buffer;
    status = io_buffer_init_physical(&buffer, device->bti, IO_BASE, IO_SIZE,
                                get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "eint: io_buffer_init_physical failed: %d\n", status);
        return status;
    }
    device->io_base = io_buffer_virt(&buffer);
    device->eint_e_base = device->io_base + EINT_E_OFFSET;
    device->gpio_din_regs = device->io_base + GPIO_DIN_OFFSET;

    device->enable_irq = IRQ_DISABLE;
    device->virq = MONITOR_VIRQ;

    zxlogf(INFO, "initialized gpio driver, regs=%p regsize=0x%lx eint_e_base:%p\n",
                device->regs, device->regs_size, device->eint_e_base);

    return ZX_OK;

fail:
    gpio_release(device);
    return status;
}

static zx_driver_ops_t gpio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gpio_bind,
};

ZIRCON_DRIVER_BEGIN(gpio, gpio_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8676),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_GPIO),
ZIRCON_DRIVER_END(gpio)