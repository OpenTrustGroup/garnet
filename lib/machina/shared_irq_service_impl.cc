// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared_irq_service_impl.h"

#include "garnet/lib/machina/guest.h"

namespace machina {

SharedIrqServiceImpl::SharedIrqServiceImpl(
    component::ApplicationContext* application_context,
    const std::vector<SharedIrqSpec>& shared_irq_specs,
    Guest* guest)
    : shared_irq_specs_(shared_irq_specs), guest_(guest) {
  application_context->outgoing_services()->AddService<SharedIrqService>(
      [this](fidl::InterfaceRequest<SharedIrqService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void SharedIrqServiceImpl::RegisterSharedIrqListener(
    fidl::InterfaceHandle<SharedIrqListener> handle,
    RegisterSharedIrqListenerCallback callback) {
  listener_.Bind(std::move(handle));
  callback();
}

void SharedIrqServiceImpl::PopulateSharedIrq(
    PopulateSharedIrqCallback callback) {
  if (!listener_.is_bound())
    return;

  for (auto& spec : shared_irq_specs_) {
    // TODO: currently only one UOS is assumed. Thus, bitmask not interested by
    // SOS will be assumed to be interested by UOS.
    uint32_t non_interested = ~spec.interested_bitmask;
    listener_->OnNewSharedIrq(spec.vector, non_interested);
  }
  callback();
}

void SharedIrqServiceImpl::UpdatePreference(uint32_t irq_nr,
                                            uint32_t interested_bitmask,
                                            UpdatePreferenceCallback callback) {
  guest_->UpdateSharedIrqPreference(irq_nr, interested_bitmask);
  callback();
}

void SharedIrqServiceImpl::ForwardIrqEvent(uint16_t vector,
                                           uint32_t pending_bitmask) {
  if (!listener_.is_bound())
    return;

  listener_->OnPendingIrqEvent(vector, pending_bitmask);
}

}  // namespace machina