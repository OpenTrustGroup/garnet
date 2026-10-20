/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/i2c.h>

#include <linux/platform_device.h>

#include "lib/nebula/base/logging.h"
#include "garnet/lib/linux/singleton.h"

#include <vector>
#include <libfdt.h>

class PlatformDeviceContext : public Singleton<PlatformDeviceContext> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  int num_irqs() const { return static_cast<int>(irq_handles_.size()); }
  int num_resources() const { return static_cast<int>(resources_.size()); }
  int id() const { return dev_info_.did; }

  resource* get_resource(unsigned int num) {
    if (num >= resources_.size()) {
      LOG(ERROR) << "Invalid resource number: " << num;
      return nullptr;
    }
    return &resources_[num];
  }

  zx_handle_t get_irq_handle(unsigned int num) {
    if (num >= irq_handles_.size()) {
      LOG(ERROR) << "Invalid IRQ number: " << num;
      return -EINVAL;
    }
    return irq_handles_[num];
  }

  platform_device* get_linux_pdev() { return &linux_pdev_; }
  platform_bus_protocol_t* pbus_proto() { return &pbus_; }
  zx_device_t* parent_device() { return parent_; }

 private:
  void Init(zx_device_t* parent) {
    parent_ = parent;

    auto status =
        device_get_protocol(parent_, ZX_PROTOCOL_PLATFORM_DEV, (void*)&pdev_);
    CHECK(status == ZX_OK) << "ZX_PROTOCOL_PLATFORM_DEV not available";

    status =
        device_get_protocol(parent_, ZX_PROTOCOL_PLATFORM_BUS, (void*)&pbus_);
    CHECK(status == ZX_OK) << "ZX_PROTOCOL_PLATFORM_BUS not available";

    status = pdev_get_device_info(&pdev_, &dev_info_);
    CHECK(status == ZX_OK) << "pdev_get_device_info failed";

    for (int i = 0; i < dev_info_.irq_count; i++) {
      zx_handle_t irq_handle;
      status = pdev_map_interrupt(&pdev_, i, &irq_handle);
      CHECK(status == ZX_OK) << "pdev_map_interrupt failed";

      irq_handles_.push_back(irq_handle);
    }

    for (int i = 0; i < dev_info_.mmio_count; i++) {
      struct resource mmio;
      status = pdev_map_mmio(&pdev_, i, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                             &mmio.vaddr, &mmio.size, &mmio.vmo_handle);
      CHECK(status == ZX_OK) << "pdev_map_mmio failed";
      resources_.push_back(mmio);
    }

    pbus_get_fdt(&pbus_, &fdt_);
  }

  std::vector<zx_handle_t> irq_handles_;
  std::vector<struct resource> resources_;
  zx_device_t* parent_;
  platform_bus_protocol_t pbus_;
  platform_device_protocol_t pdev_;
  pdev_device_info_t dev_info_;
  void* fdt_ = nullptr;

  platform_device linux_pdev_ = {};
};