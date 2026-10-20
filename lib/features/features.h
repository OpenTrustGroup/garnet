// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FEATURES_FEATURES_H_
#define GARNET_LIB_FEATURES_FEATURES_H_

#include <stddef.h>

#include <zircon/types.h>

namespace features {

class FeatureSet {
 public:
  struct Values {
    bool hyp_disable_uart_log = false;
    bool nebulalog_enabled = false;
    bool vmlog_enabled = false;
    bool virtio_console_enabled = false;
    bool mmio_bus_enabled = false;
    bool pci_bus_enabled = false;
    bool virtio_cluster_enabled = false;
    bool virtio_spi_enabled = false;
    bool virtio_i2c_enabled = false;
    bool virtio_eint_enabled = false;
    bool virtio_rtc_enabled = false;
    bool mmio_virtio_block_enabled = false;
    bool pci_virtio_block_enabled = false;
    bool virtio_vsock_enabled = false;
    bool gzfs_enabled = false;
    bool ufs_enabled = false;
    bool smc_enabled = false;
    bool pvblk_enabled = false;
    bool cvm_lock_enabled = false;
    bool wdt_sink_enabled = false;
  };

  static zx_status_t LoadFromSysinfo(FeatureSet* out);
  static zx_status_t ParseDtb(const void* dtb, size_t size, FeatureSet* out);
  static FeatureSet FromValues(Values values);

  const Values& values() const { return values_; }

  bool uartlog_disabled() const { return values_.hyp_disable_uart_log; }
  bool nebulalog_enabled() const { return values_.nebulalog_enabled; }
  bool vmlog_enabled() const { return values_.vmlog_enabled; }
  bool virtio_console_enabled() const {
    return values_.virtio_console_enabled;
  }
  bool mmio_bus_enabled() const { return values_.mmio_bus_enabled; }
  bool pci_bus_enabled() const { return values_.pci_bus_enabled; }
  bool virtio_cluster_enabled() const {
    return values_.virtio_cluster_enabled;
  }
  bool virtio_spi_enabled() const { return values_.virtio_spi_enabled; }
  bool virtio_i2c_enabled() const { return values_.virtio_i2c_enabled; }
  bool virtio_eint_enabled() const { return values_.virtio_eint_enabled; }
  bool virtio_rtc_enabled() const { return values_.virtio_rtc_enabled; }
  bool mmio_virtio_block_enabled() const {
    return values_.mmio_virtio_block_enabled;
  }
  bool pci_virtio_block_enabled() const {
    return values_.pci_virtio_block_enabled;
  }
  bool virtio_vsock_enabled() const {
    return values_.virtio_vsock_enabled;
  }
  bool gzfs_enabled() const { return values_.gzfs_enabled; }
  bool ufs_enabled() const { return values_.ufs_enabled; }
  bool smc_enabled() const { return values_.smc_enabled; }
  bool pvblk_enabled() const { return values_.pvblk_enabled; }
  bool cvm_lock_enabled() const { return values_.cvm_lock_enabled; }
  bool wdt_sink_enabled() const { return values_.wdt_sink_enabled; }

 private:
  struct FeatureSpec {
    const char* path[2];
    size_t path_len;
    bool Values::*enabled;
  };

  static const FeatureSpec* FeatureSpecs(size_t* count);

  Values values_;
};

}  // namespace features

#endif  // GARNET_LIB_FEATURES_FEATURES_H_
