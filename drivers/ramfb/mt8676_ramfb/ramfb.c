// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/pci.h>
#include <hw/reg.h>
#include <libfdt.h>
#include <threads.h>

#include <assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/device/grt-ramfb.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ramfb.h"

// #define __IRQ_DEBUG__

#define DTB_BASE_ADDRESS  0x47c80000
#define DTB_SIZE          0x800000


typedef struct ramfb_device {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    void* regs;
    void* ovl0_regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;
    zx_handle_t bti;
    uint32_t fb_pa[2];
    uint64_t fb_va[2];
    io_buffer_t fb_buffers[2];
    io_buffer_t ovl0_buffer;
    uint32_t fb_size;
    uint64_t rsvd_addr;
    uint64_t rsvd_size;
    zx_display_info_t info;
    mtx_t lock;
    /* display interrupt */
    thrd_t irq_thread;
    zx_handle_t irq_handle;
    zx_handle_t event;
    zx_handle_t vsync_event;
    uint32_t irq;
#ifdef __IRQ_DEBUG__
    uint64_t irq_count;
#endif
    pdev_irq_t out_irq;
    uint32_t num_buffers;
    uint32_t fb_init;
} ramfb_device_t;

#ifdef __DEBUG__
// for debug double buffer
typedef void (*timer_callback_t)(void* arg);
typedef struct {
    zx_duration_t interval;
    timer_callback_t callback;
    void* arg;
    bool stop;
} driver_timer_t;

static int timer_thread(void* arg) {
    driver_timer_t* t = (driver_timer_t*)arg;
    zx_handle_t timer;
    zx_timer_create(0, ZX_CLOCK_MONOTONIC, &timer);

    while (!t->stop) {
        zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
        zx_timer_set(timer, now + t->interval, 0);

        zx_object_wait_one(timer, ZX_TIMER_SIGNALED, ZX_TIME_INFINITE, NULL);

        if (t->callback && !t->stop) {
            t->callback(t->arg);
        }
    }

    zx_handle_close(timer);
    free(t);
    return 0;
}

driver_timer_t* driver_start_timer(zx_duration_t interval,
                                   timer_callback_t callback,
                                   void* arg) {
    driver_timer_t* t = malloc(sizeof(driver_timer_t));
    t->interval = interval;
    t->callback = callback;
    t->arg = arg;
    t->stop = false;

    thrd_t tid;
    if (thrd_create(&tid, timer_thread, t) == thrd_success) {
        thrd_detach(tid);
    } else {
        free(t);
        return NULL;
    }

    return t;
}

void driver_stop_timer(driver_timer_t* t) {
    if (!t) return;
    t->stop = true;
}
#endif

static void parse_reserved_addr(ramfb_device_t* device, const void *dtb, const char *node_path, uint32_t idx) {
    const char *property = "reg";
    uint64_t addr, size;
    int offset = fdt_path_offset(dtb, node_path);

    if (offset < 0) {
        zxlogf(ERROR, "Node '%s' not found: %s\n", node_path, fdt_strerror(offset));
        return;
    }

    int len;
    const uint32_t *reg = fdt_getprop(dtb, offset, property, &len);
    if (!reg) {
        zxlogf(ERROR, "Property '%s' not found in node '%s'\n", property, node_path);
        return;
    }

    zxlogf(INFO, "Node: %s\n", node_path);
    zxlogf(INFO, "Property '%s' values:\n", property);

    for (int i = 0; i < len / sizeof(uint32_t); i += 4) {
        if (i + 3 < len / sizeof(uint32_t)) {
            uint64_t addr_high = fdt32_to_cpu(reg[i]);
            uint64_t addr_low = fdt32_to_cpu(reg[i + 1]);
            uint64_t size_high = fdt32_to_cpu(reg[i + 2]);
            uint64_t size_low = fdt32_to_cpu(reg[i + 3]);

            addr = (addr_high << 32) | addr_low;
            size = (size_high << 32) | size_low;
        }
    }

    /* Map the physical buffer to the virtual address */
    io_buffer_t buffer;
    zx_status_t status = io_buffer_init_physical(&buffer, device->bti, addr, size, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "get_framebuffer_addr: io_buffer_init_physical failed: %d\n", status);
        return;
    }

    void *ptr = io_buffer_virt(&buffer);

    device->rsvd_addr = addr;
    device->rsvd_size = size;

    zxlogf(INFO, "reserve buffer pa:0x%lx size:0x%lx\n",
                            device->rsvd_addr, device->rsvd_size);
}

static void parse_fb_addr(ramfb_device_t* device, const void *dtb) {
    uint32_t fb_1_pa, fb_2_pa, fb_size;
    int offset;
    const void *val;

    offset = fdt_path_offset(dtb, "/chosen");
    if (offset < 0) {
        zxlogf(ERROR, "Node '%s' not found: %s\n", "/chosen", fdt_strerror(offset));
        return;
    }

    val = fdt_getprop(dtb, offset, "atag,nbl_fb_1_pa", NULL);
    if (val) {
        memcpy(&fb_1_pa, val, sizeof(fb_1_pa));
        zxlogf(INFO, "parse dts fb_1_pa:0x%x\n", fb_1_pa);
    } else {
        zxlogf(ERROR, "default dsi nbl_fb_1_pa in dtb is empty\n");
        return;
    }

    val = fdt_getprop(dtb, offset, "atag,nbl_fb_2_pa", NULL);
    if (val) {
        memcpy(&fb_2_pa, val, sizeof(fb_2_pa));
        zxlogf(INFO, "parse dts fb_2_pa:0x%x\n", fb_2_pa);
    } else {
        zxlogf(ERROR, "default dsi nbl_fb_2_pa in dtb is empty\n");
        return;
    }

    val = fdt_getprop(dtb, offset, "atag,nbl_fb_size", NULL);
    if (val) {
        memcpy(&fb_size, val, sizeof(fb_size));
        zxlogf(INFO, "parse dts nbl_fb_size:0x%x\n", fb_size);
    } else {
        zxlogf(ERROR, "default dsi fb_size in dtb is empty\n");
        return;
    }

    device->fb_pa[0] = fb_1_pa;
    device->fb_pa[1] = fb_2_pa;
    device->fb_size = fb_size;
    zxlogf(INFO, "fb_1_pa: 0x%x fb_1_pa: 0x%x fb_size:0x%x\n", device->fb_pa[0], device->fb_pa[1], device->fb_size);

    zx_status_t status = io_buffer_init_physical(&device->fb_buffers[0], device->bti, device->fb_pa[0], device->fb_size, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "fb 1 addr io_buffer_init_physical failed : %d\n", status);
        return;
    }

    void *ptr = io_buffer_virt(&device->fb_buffers[0]);
    device->fb_va[0] = (uint64_t)ptr;

    status = io_buffer_init_physical(&device->fb_buffers[1], device->bti, device->fb_pa[1], device->fb_size, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "fb 2 addr io_buffer_init_physical failed: %d\n", status);
        io_buffer_release(&device->fb_buffers[0]);
        device->fb_va[0] = 0;
        return;
    }
    ptr = io_buffer_virt(&device->fb_buffers[1]);
    device->fb_va[1] = (uint64_t)ptr;

    zxlogf(INFO, "fb1 buffer pa:0x%x va:0x%lx size:0x%x\n",
                            device->fb_pa[0], device->fb_va[0], device->fb_size);
    zxlogf(INFO, "fb2 buffer pa:0x%x va:0x%lx size:0x%x\n",
                            device->fb_pa[1], device->fb_va[1], device->fb_size);

    device->fb_init = 1;
}

static void parse_screen_info(ramfb_device_t* device, const void *dtb) {
    const uint32_t *prop;
    int width, height;
    int len, offset;
    const void *val;

    offset = fdt_path_offset(dtb, "/chosen");
    if (offset < 0) {
        zxlogf(ERROR, "Node '%s' not found: %s\n", "/chosen", fdt_strerror(offset));
        return;
    }

    val = fdt_getprop(dtb, offset, "atag,primary_display_width", NULL);
    if (val) {
        memcpy(&width, val, sizeof(width));
        zxlogf(INFO, "default dsi primary_display_width = <0x%x> before lk set\n", width);
    } else {
        zxlogf(ERROR, "default dsi primary_display_width in dtb is empty\n");
        return;
    }

    val = fdt_getprop(dtb, offset, "atag,primary_display_height", NULL);
    if (val) {
        memcpy(&height, val, sizeof(height));
        zxlogf(INFO, "default dsi primary_display_height = <0x%x> before lk set\n", height);
    } else {
        zxlogf(ERROR, "default dsi primary_display_height dtb is empty\n");
        return;
    }

    device->info.width = width;
    device->info.height = height;

    zxlogf(INFO, "screen width:%d height:%d \n", device->info.width, device->info.height);
}

static int parse_dtb(ramfb_device_t* device) {
    int ret = ZX_OK;
    const fdt32_t *prop;
    int len;

    io_buffer_t buffer;
    zx_status_t status = io_buffer_init_physical(&buffer, device->bti, DTB_BASE_ADDRESS, DTB_SIZE,
                                get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "get_framebuffer_addr: io_buffer_init_physical failed: %d\n", status);
        return status;
    }

    void *fdt = io_buffer_virt(&buffer);

    ret = fdt_check_header(fdt);
    if (ret != 0) {
        zxlogf(ERROR, "Invalid device tree: %s\n", fdt_strerror(ret));
        goto fail;
    }

    // parse_reserved_addr(device, fdt, "/reserved-memory/mblock-framebuffer", 0);
    parse_fb_addr(device, fdt);
    parse_screen_info(device,fdt);

    ret = ZX_OK;

fail:
    io_buffer_release(&buffer);
    return ret;
}

static void ramfb_release(void* ctx) {
    ramfb_device_t* vdev = ctx;
    zxlogf(INFO, "ramfb_unbind\n");

    if (vdev->regs) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)vdev->regs,
                       vdev->regs_size);
        vdev->regs = NULL;
        zx_handle_close(vdev->regs_handle);
        vdev->regs_handle = -1;
    }
    if (vdev->fb_va[0]) {
        io_buffer_release(&vdev->fb_buffers[0]);
        vdev->fb_va[0] = 0;
    }
    if (vdev->fb_va[1]) {
        io_buffer_release(&vdev->fb_buffers[1]);
        vdev->fb_va[1] = 0;
    }
    if (vdev->ovl0_regs) {
        io_buffer_release(&vdev->ovl0_buffer);
        vdev->ovl0_regs = NULL;
    }
    zx_handle_close(vdev->event);
    zx_handle_close(vdev->vsync_event);

    free(vdev);
}

#ifdef __DEBUG__
void ramfb_timer_task(void* arg) {
    ramfb_device_t* dev = (ramfb_device_t*)arg;
    unsigned int curr_addr;
    zx_status_t status;

    curr_addr = DISP_REG_GET(dev, DISP_REG_OVL_L0_ADDR);

    status = zx_object_signal(dev->vsync_event, 0, ZX_USER_SIGNAL_0);

    if (curr_addr == dev->fb_pa[0]) {
        DISP_REG_SET(dev, DISP_REG_OVL_L0_ADDR, dev->fb_pa[1]);
    } else {
        DISP_REG_SET(dev, DISP_REG_OVL_L0_ADDR, dev->fb_pa[0]);
    }
}
#endif

void ramfb_irq_handler(ramfb_device_t* dev) {
    unsigned int curr_addr;
    unsigned int status = DISP_REG_GET(dev, DISP_REG_OVLSYS0_MUTEX_INTSTA);

    mtx_lock(&dev->lock);

    DISP_REG_SET(dev, DISP_REG_OVLSYS0_MUTEX_INTSTA, 0);

    if (!dev->fb_pa[0] || !dev->fb_pa[1]) {
        mtx_unlock(&dev->lock);
        return;
    }

    curr_addr = DISP_REG_GET(dev, DISP_REG_OVL_L0_ADDR);

#ifdef __IRQ_DEBUG__
    if (((dev->irq_count / 100) % 2) == 0) {
        zxlogf(INFO, "ramfb_irq_handler: 0x%x count:%lu\n", curr_addr, dev->irq_count);
    }
    dev->irq_count++;
#endif

    zx_object_signal(dev->vsync_event, 0, ZX_USER_SIGNAL_0);

    if (curr_addr == dev->fb_pa[0]) {
        DISP_REG_SET(dev, DISP_REG_OVL_L0_ADDR, dev->fb_pa[1]);
    } else {
        DISP_REG_SET(dev, DISP_REG_OVL_L0_ADDR, dev->fb_pa[0]);
    }

    mtx_unlock(&dev->lock);

    return;
}

static zx_status_t ramfb_irq_thread(void* arg) {
    zx_status_t wait_ret;
    ramfb_device_t* dev = (ramfb_device_t*)arg;

    while (true) {
        uint64_t slots;
        wait_ret = zx_interrupt_wait(dev->irq_handle, &slots);
        if (wait_ret != ZX_OK) {
            zxlogf(ERROR, "ramfb: ======zx_interrupt_wait failed=====>wait_ret:%d\n", wait_ret);
            break;
        }
        ramfb_irq_handler(dev);
    }
    return ZX_OK;
}

static zx_status_t ramfb_irq_init(ramfb_device_t *dev) {
    zx_status_t status;
    unsigned int val;

    val = DISP_REG_GET(dev, DISP_REG_OVLSYS0_MUTEX_INTEN) | 0x80008000;
    DISP_REG_SET(dev, DISP_REG_OVLSYS0_MUTEX_INTEN, val);
    DISP_REG_SET(dev, DISP_REG_OVLSYS0_MUTEX15_CTL, 0x41);
    DISP_REG_SET(dev, DISP_REG_OVLSYS0_MUTEX_CFG, 0x0);
    DISP_REG_SET(dev, DISP_REG_OVLSYS0_MUTEX15, 0x1);

    status = pdev_map_interrupt(&dev->pdev, 0, &dev->irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: pdev_map_interrupt failed\n");
        return status;
    }

    pdev_get_irq_info(&dev->pdev, 0, &dev->out_irq);
    dev->irq = dev->out_irq.irq;
    zxlogf(INFO, "display interrupt:%u\n", dev->irq);

    zx_interrupt_unmask(dev->irq_handle, dev->irq);
    status = thrd_create_with_name(&dev->irq_thread, ramfb_irq_thread, dev,
                                   "ramfb-irq");
    if (status != thrd_success) {
        zxlogf(ERROR, "ramfb: failed to create irq thread\n");
        zx_handle_close(dev->irq_handle);
        return status;
    }
    thrd_detach(dev->irq_thread);

    return ZX_OK;
}

static zx_status_t set_hw_mode(ramfb_device_t* dev) {
    dev->info.format = ZX_PIXEL_FORMAT_ARGB_8888;
    dev->info.stride = dev->info.width;
    dev->info.pixelsize = ZX_PIXEL_FORMAT_BYTES(dev->info.format);
    return ZX_OK;
}

// implement display protocol
static zx_status_t ramfb_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    ramfb_device_t* ramfb = ctx;
    zx_status_t status;

    switch (op) {
    case IOCTL_GRT_RAMFB_GET_EVENT: {
        if (out_len != sizeof(zx_handle_t)) {
            zxlogf(ERROR, "ZX_ERR_BUFFER_TOO_SMALL\n");
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        zx_handle_t* out = (zx_handle_t*) out_buf;
        if ((status = zx_handle_duplicate(ramfb->event, ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL, out)) < 0) {
            zxlogf(ERROR, "zx_handle_duplicate :%d\n", status);
            return status;
        } else {
            *out_actual = sizeof(zx_handle_t);
            return ZX_OK;
        }
    }
    case IOCTL_GRT_RAMFB_GET_VSYNC_EVENT: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t* out = (zx_handle_t*) out_buf;
        if ((status = zx_handle_duplicate(ramfb->vsync_event, ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_SIGNAL, out)) < 0) {
            return status;
        } else {
            *out_actual = sizeof(zx_handle_t);
            return ZX_OK;
        }
    }
    case IOCTL_GRT_RAMFB_RELEASE: {
        /* The hyper meter needs to be running all the time. do not disable interrupt. 
         *zxlogf(INFO, "ramfb release\n");
         *zx_interrupt_mask(ramfb->irq_handle, ramfb->irq);
         *zx_handle_close(ramfb->irq_handle);
         *device_remove(ramfb->zxdev);
         *zxlogf(INFO, "ramfb: Yocto has started disable interrupts etc\n");
        */
        zxlogf(INFO, "ramfb: yocto has started to exit the hyper cluster\n");
        status = zx_object_signal(ramfb->event, 0, ZX_USER_SIGNAL_0);
        if (status != ZX_OK) {
            zxlogf(ERROR, "signal failed: %d\n", status);
        }
        return ZX_OK;
    }
    case IOCTL_GRT_RAMFB_SET_CLUSTER_HIDE: {
        if (in_len < sizeof(bool)) {
            return ZX_ERR_INVALID_ARGS;
        }

        mtx_lock(&ramfb->lock);
        bool hide = *(bool *)in_buf;
        uint32_t value = readl(ramfb->ovl0_regs + 0x2C);
        if (hide) {
            value &= 0xfffffffd;
            writel(value, ramfb->ovl0_regs + 0x2C);
            writel(0x0, ramfb->ovl0_regs + 0xe0);
            zxlogf(TRACE, "ramfb: hide cluster. ovl0_regs:0x%x\n", readl(ramfb->ovl0_regs + 0x2C));
        } else {
            value |= (0x1 << 1);
            writel(value, ramfb->ovl0_regs + 0x2C);
            writel(0x1, ramfb->ovl0_regs + 0xe0);
            zxlogf(TRACE, "ramfb: disp cluster. ovl0_regs:0x%x\n", readl(ramfb->ovl0_regs + 0x2C));
        }
        mtx_unlock(&ramfb->lock);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void ramfb_unbind(void* ctx) {
    ramfb_device_t* vdev = ctx;
    zxlogf(INFO, "ramfb_unbind\n");
    device_remove(vdev->zxdev);
}

static zx_status_t ramfb_set_mode(void* ctx, zx_display_info_t* info) {
    assert(info);
    zx_status_t status = ZX_OK;
    ramfb_device_t* vdev = ctx;

    if (memcmp(&vdev->info, info, sizeof(zx_display_info_t)) != 0) {
        memcpy(&vdev->info, info, sizeof(zx_display_info_t));
        status = set_hw_mode(vdev);
    }
    return status;
}

static zx_status_t ramfb_get_mode(void* ctx, zx_display_info_t* info) {
    assert(info);
    ramfb_device_t* vdev = ctx;
    memcpy(info, &vdev->info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t ramfb_get_framebuffer(void* ctx, void** framebuffer) {
    assert(framebuffer);
    ramfb_device_t* vdev = ctx;

    if (vdev->fb_init) {
        (*framebuffer) = (void *)(uintptr_t)vdev->fb_va[0];
        return ZX_OK;
    } else {
        zxlogf(ERROR, "ramfb: framebuffer init error\n");
        return ZX_ERR_NOT_FOUND;
    }
}

static zx_status_t ramfb_get_multi_framebuffer(void* ctx, uint32_t index, void** framebuffer) {
    assert(framebuffer);
    ramfb_device_t* vdev = ctx;

    if (!vdev->fb_init) {
        zxlogf(ERROR, "ramfb: framebuffer init error\n");
        return ZX_ERR_NOT_FOUND;
    }

    if (index >= 2) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    *framebuffer = (void *)(uintptr_t)vdev->fb_va[index];
    return ZX_OK;
}

static uint32_t ramfb_get_num_buffers(void* ctx) {
    ramfb_device_t* vdev = ctx;
    return vdev->num_buffers;
}

static void ramfb_flush(void* ctx) {
    ramfb_device_t* vdev = ctx;
    zx_signals_t observed;

    uint32_t frame_interval_ms = 1000 / TARGET_FRAME_RATE + FRAME_TIMEOUT_MARGIN_MS;

    zx_status_t status = zx_object_wait_one(vdev->vsync_event,
                                            ZX_USER_SIGNAL_0,
                                            zx_deadline_after(ZX_MSEC(frame_interval_ms)),
                                            &observed);
    if (status == ZX_OK && (observed & ZX_USER_SIGNAL_0)) {
        zx_object_signal(vdev->vsync_event, ZX_USER_SIGNAL_0, 0);
    } else if (status == ZX_ERR_TIMED_OUT) {
        zxlogf(ERROR, "VSYNC wait timeout, frame rate might be too low.\n");
    }
}

static display_protocol_ops_t ramfb_display_ops_proto = {
    .set_mode = ramfb_set_mode,
    .get_mode = ramfb_get_mode,
    .get_framebuffer = ramfb_get_framebuffer,
    .get_multi_framebuffer = ramfb_get_multi_framebuffer,
    .get_num_buffers = ramfb_get_num_buffers,
    .flush = ramfb_flush,
};

// implement device protocol

static zx_protocol_device_t ramfb_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ramfb_ioctl,
    .release = ramfb_release,
    .unbind = ramfb_unbind,
};

static zx_status_t ramfb_bind(void* ctx, zx_device_t* dev) {
    zx_status_t status;
    zx_handle_t port;

    // map resources and initialize the device
    ramfb_device_t* device = calloc(1, sizeof(ramfb_device_t));
    if (!device)
        return ZX_ERR_NO_MEMORY;

    if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV,
            (void*) &device->pdev))
        return ZX_ERR_NOT_SUPPORTED;

    status = pdev_get_bti(&device->pdev, 0, &device->bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: could not get BTI handle: %d\n", status);
        goto fail;
    }

    // map register window
    status = pdev_map_mmio(&device->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->regs, &device->regs_size,
                              &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: failed to map firmware config: %d\n", status);
        goto fail;
    }

    status = parse_dtb(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: parse dtb error: %d\n", status);
        goto fail;
    }

    status = set_hw_mode(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: set_hw_mode error: %d\n", status);
        goto fail;
    }

    if ((status = zx_event_create(0, &device->vsync_event)) < 0) {
        zxlogf(ERROR, "cannot create vsync event: %d\n", status);
        goto fail;
    }

    /* Must be set before enable interrupt */
    device->num_buffers = MAX_FBS;

    status = ramfb_irq_init(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: init interrupt error: %d\n", status);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "mtk_ramfb",
        .ctx = device,
        .ops = &ramfb_device_proto,
        .proto_id = ZX_PROTOCOL_DISPLAY,
        .proto_ops = &ramfb_display_ops_proto,
        .flags = DEVICE_ADD_MUST_ISOLATE,
    };

    status = io_buffer_init_physical(&device->ovl0_buffer, device->bti, DISP_OVL0_2L_BASE, DISP_OVL0_2L_SIZE,
                                get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ramfb: io_buffer_init_physical failed: %d\n", status);
        goto fail;
    }
    device->ovl0_regs = io_buffer_virt(&device->ovl0_buffer);

    if ((status = zx_event_create(0, &device->event)) < 0) {
        zxlogf(ERROR, "cannot create event: %d\n", status);
        goto fail;
    }

    status = device_add(dev, &args, &device->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    mtx_init(&device->lock, mtx_plain);

#ifdef __DEBUG__
    driver_start_timer(ZX_MSEC(10), ramfb_timer_task, device);
#endif
    zxlogf(INFO, "initialized ramfb display driver, reg=%p regsize=0x%lx\n", device->regs, device->regs_size);

    return ZX_OK;

fail_remove:
    device_remove(device->zxdev);
    zx_handle_close(port);
fail:
    ramfb_release(device);
    return status;
}

static zx_driver_ops_t ramfb_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ramfb_bind,
};

ZIRCON_DRIVER_BEGIN(ramfb, ramfb_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8676),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_RAMFB),
ZIRCON_DRIVER_END(ramfb)
