// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interrupt_service_impl.h"

namespace machina {

InterruptServiceImpl::InterruptServiceImpl(component::ServiceProviderBridge* bridge,
                           InterruptController *interrupt_controller) :
  interrupt_controller_(interrupt_controller) {
  bridge->AddService<InterruptService>(
      [this](fidl::InterfaceRequest<InterruptService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  FXL_CHECK(loop_.StartThread() == ZX_OK);
}

void InterruptServiceImpl::MovePassthroughToUos(fidl::VectorPtr<uint16_t> irqs,
                                        MovePassthroughToUosCallback callback) {
  interrupt_controller_->MovePassthroughToUos(std::move(irqs));
  callback();
}

void InterruptServiceImpl::RaiseTimeSlicedIrq(
    uint32_t irq,
    RaiseTimeSlicedIrqCallback callback) {
  const zx_status_t status = interrupt_controller_->RaiseTimeSlicedPtIrq(irq);
  callback(static_cast<int32_t>(status));
}

}  // namespace machina
