// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <zircon/process.h>
#include <zircon/syscalls/iommu.h>

#include <libfdt.h>

#include "platform-bus.h"

static zx_status_t platform_bus_get_bti(void* ctx, uint32_t iommu_index, uint32_t bti_id,
                                        zx_handle_t* out_handle) {
    platform_bus_t* bus = ctx;
    if (iommu_index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_bti_create(bus->dummy_iommu_handle, 0, bti_id, out_handle);
}

// default IOMMU protocol to use if the board driver does not set one via pbus_set_protocol()
static iommu_protocol_ops_t platform_bus_default_iommu_ops = {
    .get_bti = platform_bus_get_bti,
};

static zx_status_t platform_bus_set_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    if (!protocol) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_bus_t* bus = ctx;

    switch (proto_id) {
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        memcpy(&bus->ums, protocol, sizeof(bus->ums));
        break;
    case ZX_PROTOCOL_GPIO:
        memcpy(&bus->gpio, protocol, sizeof(bus->gpio));
        break;
    case ZX_PROTOCOL_I2C_IMPL: {
        zx_status_t status = platform_i2c_init(bus, (i2c_impl_protocol_t *)protocol);
        if (status != ZX_OK) {
            return status;
         }
        memcpy(&bus->i2c, protocol, sizeof(bus->i2c));
        break;
    }
    case ZX_PROTOCOL_CLK:
        memcpy(&bus->clk, protocol, sizeof(bus->clk));
        break;
    case ZX_PROTOCOL_IOMMU:
        memcpy(&bus->iommu, protocol, sizeof(bus->iommu));
        break;
    case ZX_PROTOCOL_WDT:
        memcpy(&bus->wdt, protocol, sizeof(bus->wdt));
        break;
    case ZX_PROTOCOL_EINT:
        memcpy(&bus->eint, protocol, sizeof(bus->eint));
        break;
    default:
        // TODO(voydanoff) consider having a registry of arbitrary protocols
        return ZX_ERR_NOT_SUPPORTED;
    }

    completion_signal(&bus->proto_completion);
    return ZX_OK;
}

static zx_status_t platform_bus_wait_protocol(void* ctx, uint32_t proto_id) {
    platform_bus_t* bus = ctx;

    platform_bus_protocol_t dummy;
    while (platform_bus_get_protocol(bus, proto_id, &dummy) == ZX_ERR_NOT_SUPPORTED) {
        completion_reset(&bus->proto_completion);
        zx_status_t status = completion_wait(&bus->proto_completion, ZX_TIME_INFINITE);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}

static zx_status_t platform_bus_device_add(void* ctx, const pbus_dev_t* dev, uint32_t flags) {
    platform_bus_t* bus = ctx;
    return platform_device_add(bus, dev, flags);
}

static zx_status_t platform_bus_device_enable(void* ctx, uint32_t vid, uint32_t pid, uint32_t did,
                                              bool enable) {
    platform_bus_t* bus = ctx;
    platform_dev_t* dev;
    list_for_every_entry(&bus->devices, dev, platform_dev_t, node) {
        if (dev->vid == vid && dev->pid == pid && dev->did == did) {
            return platform_device_enable(dev, enable);
        }
    }

    return ZX_ERR_NOT_FOUND;
}

static const char* platform_bus_get_board_name(void* ctx) {
    platform_bus_t* bus = ctx;
    return bus->platform_id.board_name;
}

static void platform_bus_get_fdt(void* ctx, void** out_fdt) {
    platform_bus_t* bus = ctx;
    *out_fdt = bus->fdt;
}

static zx_status_t platform_bus_get_boot_item(void* ctx, uint32_t type, void* out, size_t capacity,
                                              size_t* actual) {
    platform_bus_t* bus = ctx;
    const void* data = NULL;
    size_t size = 0;

    if (!actual) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (type) {
    case BOOTDATA_DEVICETREE:
        data = bus->devicetree;
        size = bus->devicetree_size;
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    *actual = size;
    if (!data) {
        return ZX_ERR_NOT_FOUND;
    }
    if (!out || capacity < size) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(out, data, size);
    return ZX_OK;
}

static platform_bus_protocol_ops_t platform_bus_proto_ops = {
    .set_protocol = platform_bus_set_protocol,
    .wait_protocol = platform_bus_wait_protocol,
    .device_add = platform_bus_device_add,
    .device_enable = platform_bus_device_enable,
    .get_board_name = platform_bus_get_board_name,
    .get_fdt = platform_bus_get_fdt,
    .get_boot_item = platform_bus_get_boot_item,
};

// not static so we can access from platform_dev_get_protocol()
zx_status_t platform_bus_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    platform_bus_t* bus = ctx;

    switch (proto_id) {
    case ZX_PROTOCOL_PLATFORM_BUS: {
        platform_bus_protocol_t* proto = protocol;
        proto->ops = &platform_bus_proto_ops;
        proto->ctx = bus;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        if (bus->ums.ops) {
            memcpy(protocol, &bus->ums, sizeof(bus->ums));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_GPIO:
        if (bus->gpio.ops) {
            memcpy(protocol, &bus->gpio, sizeof(bus->gpio));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_I2C_IMPL:
        if (bus->i2c.ops) {
            memcpy(protocol, &bus->i2c, sizeof(bus->i2c));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_CLK:
        if (bus->clk.ops) {
            memcpy(protocol, &bus->clk, sizeof(bus->clk));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_IOMMU:
        if (bus->iommu.ops) {
            memcpy(protocol, &bus->iommu, sizeof(bus->iommu));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_EINT:
        if (bus->eint.ops) {
            memcpy(protocol, &bus->eint, sizeof(bus->eint));
            return ZX_OK;
        }
        break;
    default:
        // TODO(voydanoff) consider having a registry of arbitrary protocols
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;

    platform_dev_t* dev;
    list_for_every_entry(&bus->devices, dev, platform_dev_t, node) {
        platform_dev_free(dev);
    }

    zx_handle_close(bus->dummy_iommu_handle);
    free(bus->devicetree);

    free(bus);
}

static zx_protocol_device_t platform_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = platform_bus_get_protocol,
    .release = platform_bus_release,
};

static zx_status_t platform_bus_suspend(void* ctx, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t sys_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .suspend = platform_bus_suspend,
};

static zx_status_t platform_bus_read_bootdata(platform_bus_t* bus, zx_handle_t vmo) {
    bootdata_t bootdata;
    zx_status_t status = zx_vmo_read(vmo, &bootdata, 0, sizeof(bootdata));
    if (status != ZX_OK) {
        return status;
    }
    if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
        zxlogf(ERROR, "platform_bus: bootdata item does not contain bootdata\n");
        return ZX_ERR_INTERNAL;
    }
    if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
        zxlogf(ERROR, "platform_bus: bootdata v1 not supported\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    size_t len = bootdata.length;
    size_t off = sizeof(bootdata);
    bool platform_id_found = false;

    while (len > sizeof(bootdata)) {
        zx_status_t status = zx_vmo_read(vmo, &bootdata, off, sizeof(bootdata));
        if (status < 0) {
            break;
        }
        size_t itemlen = BOOTDATA_ALIGN(sizeof(bootdata_t) + bootdata.length);
        if (itemlen > len) {
            zxlogf(ERROR, "platform_bus: bootdata item too large (%zd > %zd)\n", itemlen, len);
            break;
        }
        switch (bootdata.type) {
        case BOOTDATA_CONTAINER:
            zxlogf(ERROR, "platform_bus: unexpected bootdata container header\n");
            return ZX_ERR_INTERNAL;
        case BOOTDATA_PLATFORM_ID:
            status = zx_vmo_read(vmo, &bus->platform_id, off + sizeof(bootdata_t),
                                 sizeof(bus->platform_id));
            if (status != ZX_OK) {
                return status;
            }
            platform_id_found = true;
            break;
        case BOOTDATA_FDT: {
            void* fdt;
            fdt = malloc(bootdata.length);
            if (!fdt) {
                return ZX_ERR_NO_MEMORY;
            }
            zxlogf(INFO, "platform_bus: device-tree detected, size=%d\n", bootdata.length);
            status = zx_vmo_read(vmo, fdt, off + sizeof(bootdata_t), bootdata.length);
            if (status != ZX_OK) {
                free(fdt);
                return status;
            }
            if (fdt_check_header(fdt) != 0) {
                zxlogf(ERROR, "platform_bus: FDT blob has invalid header\n");
                free(fdt);
                return ZX_ERR_INTERNAL;
            }
            bus->fdt = fdt;
            zxlogf(INFO, "platform_bus: device-tree loaded\n");
            break;
        }
        case BOOTDATA_DEVICETREE:
            if (!bus->devicetree && bootdata.length) {
                bus->devicetree = malloc(bootdata.length);
                if (!bus->devicetree) {
                    return ZX_ERR_NO_MEMORY;
                }
                status = zx_vmo_read(vmo, bus->devicetree, off + sizeof(bootdata_t),
                                     bootdata.length);
                if (status != ZX_OK) {
                    free(bus->devicetree);
                    bus->devicetree = NULL;
                    return status;
                }
                bus->devicetree_size = bootdata.length;
                zxlogf(INFO, "platform_bus: devicetree boot item size %zu\n",
                       bus->devicetree_size);
            }
            break;
        default:
            break;
        }
        off += itemlen;
        len -= itemlen;
    }

    if (!platform_id_found) {
        zxlogf(ERROR, "platform_bus: BOOTDATA_PLATFORM_ID not found\n");
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

static zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                       const char* args, zx_handle_t bootdata_vmo) {
    if (!args) {
        zxlogf(ERROR, "platform_bus_create: args missing\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    platform_bus_t* bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        return  ZX_ERR_NO_MEMORY;
    }
    completion_reset(&bus->proto_completion);
    bus->resource = get_root_resource();

    zx_status_t status = platform_bus_read_bootdata(bus, bootdata_vmo);
    zx_handle_close(bootdata_vmo);
    if (status != ZX_OK) {
        free(bus);
        return status;
    }

    // set up a dummy IOMMU protocol to use in the case where our board driver does not
    // set a real one.
    zx_iommu_desc_dummy_t desc;
    status = zx_iommu_create(bus->resource, ZX_IOMMU_TYPE_DUMMY,  &desc, sizeof(desc),
                             &bus->dummy_iommu_handle);
    if (status != ZX_OK) {
        free(bus);
        return status;
    }
    bus->iommu.ops = &platform_bus_default_iommu_ops;
    bus->iommu.ctx = bus;

    // This creates the "sys" device
    device_add_args_t self_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ops = &sys_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &self_args, &parent);
    if (status != ZX_OK) {
        zx_handle_close(bus->dummy_iommu_handle);
        free(bus);
        return status;
    }

    // Then we attach the platform-bus device below it
    list_initialize(&bus->devices);

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, bus->platform_id.vid},
        {BIND_PLATFORM_DEV_PID, 0, bus->platform_id.pid},
    };

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "platform",
        .ctx = bus,
        .ops = &platform_bus_proto,
        .proto_id = ZX_PROTOCOL_PLATFORM_BUS,
        .proto_ops = &platform_bus_proto_ops,
        .props = props,
        .prop_count = countof(props),
    };

   return device_add(parent, &add_args, &bus->zxdev);
}

static zx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = platform_bus_create,
};

ZIRCON_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus)
