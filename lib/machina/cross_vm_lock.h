// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2026 GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/vmo.h>

#include "garnet/lib/machina/guest.h"

namespace machina {

class CrossVMLockGMServiceImpl : public CrossVMLockGMService {
 public:
  CrossVMLockGMServiceImpl(component::ApplicationContext* application_context);

  // |CrossVMLockGMService|
  void GetVmo(GetVmoCallback callback) override;

 private:
  fidl::BindingSet<CrossVMLockGMService> bindings_;
  zx::vmo mem_vmo_;
};

class CrossVMLockSOSServiceImpl : public CrossVMLockSOSService {
 public:
  CrossVMLockSOSServiceImpl(component::ServiceProviderBridge* bridge, zx::vmo vmo);

  // |CrossVMLockSOSService|
  void GetVmo(GetVmoCallback callback) override;

 private:
  fidl::BindingSet<CrossVMLockSOSService> bindings_;
  zx::vmo mem_vmo_;
};

class CrossVMLockGMClient {
 public:
  CrossVMLockGMClient(Guest* guest);
  fidl::InterfaceRequest<CrossVMLockGMService> NewRequest() { return svc_.NewRequest(); }
  void MapVmo();
  const zx::vmo& GetVmo() const { return mem_vmo_; }
  uint64_t mem_gpaddr() { return mem_gpaddr_; }
  size_t mem_size() { return mem_size_; }

 private:
  CrossVMLockGMServiceSyncPtr svc_;
  zx::vmo mem_vmo_;
  uint64_t mem_gpaddr_;
  size_t mem_size_;
  Guest* guest_;
};

class CrossVMLockSOSClient {
 public:
  CrossVMLockSOSClient(Guest* guest);
  fidl::InterfaceRequest<CrossVMLockSOSService> NewRequest() { return svc_.NewRequest(); }
  void MapVmo();
  const zx::vmo& GetVmo() const { return mem_vmo_; }
  uint64_t mem_gpaddr() { return mem_gpaddr_; }
  size_t mem_size() { return mem_size_; }

 private:
  CrossVMLockSOSServiceSyncPtr svc_;
  zx::vmo mem_vmo_;
  uint64_t mem_gpaddr_;
  size_t mem_size_;
  Guest* guest_;
};

}  // namespace machina
