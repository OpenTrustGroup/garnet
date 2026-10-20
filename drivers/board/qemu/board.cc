// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technologies Co., Ltd. All rights reserved.
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "board.h"

#include <unistd.h>
#include <stdlib.h>

#include <zircon/status.h>
#include <zircon/threads.h>
#include <nebula/device/board.h>

#include <ddk/binding.h>
#include <ddktl/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/logging.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

namespace qemu {

static pbus_dev_t drvtest_dev = {
    .name = "drvtest",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_NBL_DRVTEST,
};

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = 0x09003000,
        .length = 0x1000,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = 252,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t imx_i2c_dev = {
    .name = "imx-i2c",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_FSL_IMX1_I2C,
    .mmios = i2c_mmios,
    .mmio_count = countof(i2c_mmios),
    .irqs = i2c_irqs,
    .irq_count = countof(i2c_irqs),
};

static const pbus_i2c_channel_t echo_i2c_channels[] = {
    {
        .bus_id = 0,
        .address = 0x39,
    },
};

static pbus_dev_t echo_i2c_dev = {
    .name = "echo-i2c",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_QEMU_ECHO_I2C,
    .i2c_channels = echo_i2c_channels,
    .i2c_channel_count = countof(echo_i2c_channels),
};

static pbus_dev_t guest_alloc_dev = {
    .name = "guest_allocator",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_GRT_GUEST_MEMORY_ALLOCATOR,
};

zx_status_t Board::PlatformDeviceEnable(const void* in_buf, size_t in_len) {
  if (in_len != sizeof(pdev_enable_info_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const pdev_enable_info_t* info =
      static_cast<const pdev_enable_info_t*>(in_buf);
  auto rc =
      pbus_device_enable(&pbus_, info->vid, info->pid, info->did, info->enable);
  if (rc != ZX_OK) {
    return rc;
  }

  std::string name =
      fxl::StringPrintf("%02x:%02x:%01x", info->vid, info->pid, info->did);
  FXL_LOG(INFO) << "pdev:" << name << " "
            << (info->enable ? "enabled" : "disabled");

  return ZX_OK;
}

#define PERIPHERAL_BASE_PHYS (0)
#define PERIPHERAL_BASE_SIZE (0x40000000UL) // 1GB
#define PERIPHERAL_BASE_VIRT (0xffffffffc0000000ULL) // -1GB
#define PCIE_MMIO_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x10000000))
#define PCIE_MMIO_SIZE      (0x2eff0000)
#define PCIE_PIO_BASE_PHYS  ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x3eff0000))
#define PCIE_PIO_SIZE       (0x00010000)
#define PCIE_ECAM_BASE_PHYS ((zx_paddr_t)(PERIPHERAL_BASE_PHYS + 0x3f000000))
#define PCIE_ECAM_SIZE      (0x01000000)
#define PCIE_INT_BASE   (32 + 3)
#define PCIE_INT_COUNT  (4)

static zx_status_t qemu_pci_init(void) {
    zx_status_t status;

    zx_pci_init_arg_t* arg;
    size_t arg_size = sizeof(*arg) + sizeof(arg->addr_windows[0]);   // room for one addr window
    arg = reinterpret_cast<zx_pci_init_arg_t*>(calloc(1, arg_size));
    if (!arg) return ZX_ERR_NO_MEMORY;

    // initialize our swizzle table
    zx_pci_irq_swizzle_lut_t* lut = &arg->dev_pin_to_global_irq;
    for (unsigned dev_id = 0; dev_id < ZX_PCI_MAX_DEVICES_PER_BUS; dev_id++) {
        for (unsigned func_id = 0; func_id < ZX_PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
            for (unsigned pin = 0; pin < ZX_PCI_MAX_LEGACY_IRQ_PINS; pin++) {
                (*lut)[dev_id][func_id][pin] = PCIE_INT_BASE +
                                               (pin + dev_id) % ZX_PCI_MAX_LEGACY_IRQ_PINS;
            }
        }
    }

    status = zx_pci_add_subtract_io_range(get_root_resource(), true /* mmio */,
                                          PCIE_MMIO_BASE_PHYS, PCIE_MMIO_SIZE, true /* add */);
    if (status != ZX_OK) {
        goto fail;
    }
    status = zx_pci_add_subtract_io_range(get_root_resource(), false /* pio */,
                                          PCIE_PIO_BASE_PHYS, PCIE_PIO_SIZE, true /* add */);
    if (status != ZX_OK) {
        goto fail;
    }

    arg->num_irqs = 0;
    arg->addr_window_count = 1;
    arg->addr_windows[0].is_mmio = true;
    arg->addr_windows[0].has_ecam = true;
    arg->addr_windows[0].base = PCIE_ECAM_BASE_PHYS;
    arg->addr_windows[0].size = PCIE_ECAM_SIZE;
    arg->addr_windows[0].bus_start = 0;
    arg->addr_windows[0].bus_end = (PCIE_ECAM_SIZE / ZX_PCI_ECAM_BYTE_PER_BUS) - 1;

    status = zx_pci_init(get_root_resource(), arg, arg_size);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << fxl::StringPrintf("%s: error %d in zx_pci_init", __FUNCTION__, status);
        goto fail;
    }

fail:
    free(arg);
    return status;
}

zx_status_t Board::DdkIoctl(uint32_t op,
                            const void* in_buf,
                            size_t in_len,
                            void* out_buf,
                            size_t out_len,
                            size_t* actual) {
  switch (op) {
    case IOCTL_NBL_BOARD_PDEV_ENABLE:
      return PlatformDeviceEnable(in_buf, in_len);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Board::Bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  platform_bus_protocol_t pbus;

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get protocol: " << zx_status_get_string(status);
    return status;
  }

  auto board = std::make_unique<Board>(parent, pbus);
  if (!board) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("nbl_board", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = pbus_device_add(&pbus, &drvtest_dev, PDEV_ADD_DISABLED)) != ZX_OK) {
    FXL_LOG(ERROR) << "pbus_device_add failed: " << zx_status_get_string(status);
    return status;
  }

  if ((status = pbus_device_add(&pbus, &imx_i2c_dev, PDEV_ADD_PBUS_DEVHOST)) != ZX_OK) {
    FXL_LOG(ERROR) << "pbus_device_add failed: " << zx_status_get_string(status);
    return status;
  }

  if ((status = pbus_device_add(&pbus, &echo_i2c_dev, 0)) != ZX_OK) {
    FXL_LOG(ERROR) << "pbus_device_add failed: " << zx_status_get_string(status);
    return status;
  }

  if ((status = pbus_device_add(&pbus, &guest_alloc_dev, PDEV_ADD_PBUS_DEVHOST)) != ZX_OK) {
    FXL_LOG(ERROR) << "pbus_device_add failed: " << zx_status_get_string(status);
    return status;
  }

  if ((status = qemu_pci_init()) != ZX_OK) {
    FXL_LOG(ERROR) << "qemu_pci_init failed: " << zx_status_get_string(status);
    return status;
  }

  pbus_bti_t pci_btis[] = {
	  {
		  .iommu_index = 0,
		  .bti_id = 0,
	  },
  };

  pbus_dev_t pci_dev = {
	  .name = "pci",
	  .vid = PDEV_VID_GENERIC,
	  .pid = PDEV_PID_GENERIC,
	  .did = PDEV_DID_KPCI,
	  .btis = pci_btis,
	  .bti_count = countof(pci_btis),
  };

  status = pbus_device_add(&pbus, &pci_dev, 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "pbus_device_add failed: " << zx_status_get_string(status);
  }

  __UNUSED auto* dummy = board.release();
  return ZX_OK;
}

}  // namespace qemu

static constexpr zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = qemu::Board::Bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(board, driver_ops, "qemu", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QEMU),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_QEMU),
ZIRCON_DRIVER_END(board)
