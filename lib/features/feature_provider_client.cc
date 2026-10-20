// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/features/feature_provider_client.h"

#include <zircon/errors.h>

#include <memory>
#include <utility>

#include <fuchsia/cpp/features.h>

#include "garnet/lib/callback/cancellable_helper.h"

namespace features {
namespace {

FeatureSet FromFidlFeatureState(const FeatureState& state) {
  FeatureSet::Values values;
  values.hyp_disable_uart_log = state.hyp_disable_uart_log;
  values.nebulalog_enabled = state.nebulalog_enabled;
  values.vmlog_enabled = state.vmlog_enabled;
  values.virtio_console_enabled = state.virtio_console_enabled;
  values.mmio_bus_enabled = state.mmio_bus_enabled;
  values.pci_bus_enabled = state.pci_bus_enabled;
  values.virtio_cluster_enabled = state.virtio_cluster_enabled;
  values.virtio_spi_enabled = state.virtio_spi_enabled;
  values.virtio_i2c_enabled = state.virtio_i2c_enabled;
  values.virtio_eint_enabled = state.virtio_eint_enabled;
  values.virtio_rtc_enabled = state.virtio_rtc_enabled;
  values.mmio_virtio_block_enabled = state.mmio_virtio_block_enabled;
  values.pci_virtio_block_enabled = state.pci_virtio_block_enabled;
  values.virtio_vsock_enabled = state.virtio_vsock_enabled;
  values.gzfs_enabled = state.gzfs_enabled;
  values.ufs_enabled = state.ufs_enabled;
  values.smc_enabled = state.smc_enabled;
  values.pvblk_enabled = state.pvblk_enabled;
  values.cvm_lock_enabled = state.cvm_lock_enabled;
  values.wdt_sink_enabled = state.wdt_sink_enabled;
  return FeatureSet::FromValues(values);
}

class FeatureProviderRequest
    : public std::enable_shared_from_this<FeatureProviderRequest> {
 public:
  static fxl::RefPtr<callback::Cancellable> Start(
      component::ApplicationContext* context, LoadFeaturesCallback callback) {
    auto request = std::shared_ptr<FeatureProviderRequest>(
        new FeatureProviderRequest(context));
    auto cancellable = callback::CancellableImpl::Create(
        [request] { request->Cancel(); });
    request->Start(cancellable->WrapCallback(std::move(callback)));
    return cancellable;
  }

 private:
  explicit FeatureProviderRequest(component::ApplicationContext* context)
      : context_(context) {}

  void Start(LoadFeaturesCallback callback) {
    callback_ = std::move(callback);
    if (context_ == nullptr || !callback_) {
      Complete(ZX_ERR_INVALID_ARGS, FeatureState());
      return;
    }

    context_->ConnectToEnvironmentService(provider_.NewRequest());

    std::shared_ptr<FeatureProviderRequest> self = shared_from_this();
    provider_.set_error_handler([self] {
      self->Complete(ZX_ERR_PEER_CLOSED, FeatureState());
    });

    provider_->GetFeatures([self](int32_t result, FeatureState features) {
      self->Complete(result, features);
    });
  }

  void Cancel() {
    callback_ = nullptr;
    provider_.set_error_handler(nullptr);
    provider_.Unbind();
  }

  void Complete(zx_status_t status, FeatureState state) {
    if (!callback_) {
      return;
    }

    provider_.set_error_handler(nullptr);
    LoadFeaturesCallback callback = std::move(callback_);
    if (status != ZX_OK) {
      callback(status, FeatureSet());
      return;
    }

    callback(ZX_OK, FromFidlFeatureState(state));
  }

  component::ApplicationContext* context_;
  LoadFeaturesCallback callback_;
  FeatureProviderPtr provider_;
};

}  // namespace

fxl::RefPtr<callback::Cancellable> LoadFromFeatureProvider(
    component::ApplicationContext* context, LoadFeaturesCallback callback) {
  return FeatureProviderRequest::Start(context, std::move(callback));
}
}  // namespace features
