// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <fbl/auto_lock.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <array>

#include "garnet/lib/machina/guest_config.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace machina {
  class GpuPingServiceImpl : public GpuPingService {

public:
  GpuPingServiceImpl(component::ServiceProviderBridge* bridge,
                 Guest* guest);
  void GetPingIrq(uint16_t vmid, GetPingIrqCallback callback);
  void GetFifoBufVmo(uint16_t vmid, GetFifoBufVmoCallback callback);
  uint64_t fifo_size() { return fifo_size_; };
  uint64_t fifo_gpaddr() { return fifo_gpaddr_; };
  Guest* guest() { return guest_; };
  void SavePingIrq(uint32_t irq) { hwirq_ = irq; };

private:
  fidl::BindingSet<GpuPingService> bindings_;
  size_t fifo_size_;
  uint64_t fifo_gpaddr_;
  zx::vmo fifo_vmo_;
  uint32_t hwirq_;
  Guest* guest_;
};

class GpuPingClient {
public:
  GpuPingClient(Guest* guest);
  void MapVmo();
  fidl::InterfaceRequest<GpuPingService> NewRequest() { return svc_.NewRequest(); }
  zx_status_t CreateGpuPingTreeNodes(uintptr_t guest_phys_base);
  void GetPingIrq();
private:
  zx::vmo fifo_vmo_;
  GpuPingServiceSyncPtr svc_;
  async::Loop loop_;
  size_t fifo_size_;
  uint64_t fifo_gpaddr_;
  uint32_t hwirq_;
  Guest* guest_;
};

}  // namespace machina
