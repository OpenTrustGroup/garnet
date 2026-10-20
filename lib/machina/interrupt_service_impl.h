// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <thread>

#include "lib/async-loop/cpp/loop.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "lib/app/cpp/application_context.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class InterruptServiceImpl: public InterruptService {
 public:
  InterruptServiceImpl(component::ServiceProviderBridge* bridge,
               InterruptController *interrupt_controller);

  // |InterruptService|
  virtual void MovePassthroughToUos(fidl::VectorPtr<uint16_t> irqs,
                                        MovePassthroughToUosCallback callback);

  virtual void RaiseTimeSlicedIrq(uint32_t irq,
                                     RaiseTimeSlicedIrqCallback callback);
 private:
  fidl::BindingSet<InterruptService> bindings_;
  async::Loop loop_;
  InterruptController* interrupt_controller_;
};

}  // namespace machina
