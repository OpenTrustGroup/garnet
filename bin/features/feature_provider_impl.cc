// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/features/feature_provider_impl.h"

#include <zircon/errors.h>

#include <utility>

#include "lib/fxl/logging.h"

namespace features {

FeatureState ToFidlFeatureState(const FeatureSet& features) {
  const FeatureSet::Values& values = features.values();
  FeatureState state = {};
  state.hyp_disable_uart_log = values.hyp_disable_uart_log;
  state.nebulalog_enabled = values.nebulalog_enabled;
  state.vmlog_enabled = values.vmlog_enabled;
  state.virtio_console_enabled = values.virtio_console_enabled;
  state.mmio_bus_enabled = values.mmio_bus_enabled;
  state.pci_bus_enabled = values.pci_bus_enabled;
  state.virtio_cluster_enabled = values.virtio_cluster_enabled;
  state.virtio_spi_enabled = values.virtio_spi_enabled;
  state.virtio_i2c_enabled = values.virtio_i2c_enabled;
  state.virtio_eint_enabled = values.virtio_eint_enabled;
  state.virtio_rtc_enabled = values.virtio_rtc_enabled;
  state.mmio_virtio_block_enabled = values.mmio_virtio_block_enabled;
  state.pci_virtio_block_enabled = values.pci_virtio_block_enabled;
  state.virtio_vsock_enabled = values.virtio_vsock_enabled;
  state.gzfs_enabled = values.gzfs_enabled;
  state.ufs_enabled = values.ufs_enabled;
  state.smc_enabled = values.smc_enabled;
  state.pvblk_enabled = values.pvblk_enabled;
  state.cvm_lock_enabled = values.cvm_lock_enabled;
  state.wdt_sink_enabled = values.wdt_sink_enabled;
  return state;
}

FeatureProviderImpl::FeatureProviderImpl(LoadFeatures load_features)
    : load_features_(std::move(load_features)) {}

void FeatureProviderImpl::GetFeatures(GetFeaturesCallback callback) {
  zx_status_t status = EnsureLoaded();
  if (status != ZX_OK) {
    callback(status, FeatureState());
    return;
  }

  callback(ZX_OK, ToFidlFeatureState(features_));
}

zx_status_t FeatureProviderImpl::EnsureLoaded() {
  if (loaded_) {
    return ZX_OK;
  }

  FeatureSet features;
  zx_status_t status = load_features_(&features);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load platform features: " << status;
    return status;
  }

  features_ = features;
  loaded_ = true;
  return ZX_OK;
}

}  // namespace features
