// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class SharedIrqServiceImpl : public SharedIrqService {
 public:
  SharedIrqServiceImpl(component::ApplicationContext* application_context,
                       const std::vector<SharedIrqSpec>& shared_irq_specs,
                       Guest* guest);

  // |SharedIrqService|
  virtual void RegisterSharedIrqListener(
      fidl::InterfaceHandle<SharedIrqListener> listener,
      RegisterSharedIrqListenerCallback callback) override;
  virtual void PopulateSharedIrq(PopulateSharedIrqCallback callback) override;
  virtual void UpdatePreference(uint32_t irq_nr,
                                uint32_t interested_bitmask,
                                UpdatePreferenceCallback callback) override;

  void ForwardIrqEvent(uint16_t vector, uint32_t pending_bitmask);

  const std::vector<SharedIrqSpec>& shared_irq_specs() {
    return shared_irq_specs_;
  }

 private:
  fidl::BindingSet<SharedIrqService> bindings_;
  SharedIrqListenerSyncPtr listener_;
  const std::vector<SharedIrqSpec>& shared_irq_specs_;
  Guest* guest_;
};

}  // namespace machina
