// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest.h"

namespace machina {

class MemControllerClient : public MemControllerListener {
 public:
  MemControllerClient(Guest* guest);

 private:
  zx_status_t UnplugMemory(uint64_t gpa_base, uint64_t size);

  void OnPlug(uint64_t gpa_base,
              fidl::VectorPtr<uint64_t> phys_addrs,
              OnPlugCallback callback) override;
  void OnUnPlug(uint64_t gpa_base,
                uint64_t size,
                OnUnPlugCallback callback) override;
  void OnSetGpaRegion(uint64_t gpa_base,
                      uint64_t size,
                      OnSetGpaRegionCallback callback) override;

  Guest* guest_;
  Guest::GpaRange* gpa_range_ = nullptr;
  uint64_t gpa_range_base_ = 0;
  uint64_t gpa_range_size_ = 0;
  MemControllerSyncPtr svc_;
  async::Loop loop_;
  fidl::Binding<MemControllerListener> event_listener_binding_{this};
};
}  // namespace machina
