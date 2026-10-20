// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"

namespace machina {

enum {
  PLUG_STATUS_PENDING = -1,
  PLUG_STATUS_SUCCESS = 0,
  PLUG_STATUS_ERROR = 1,
  PLUG_STATUS_ERROR_INVALID_COUNT = 2,
  PLUG_STATUS_ERROR_LISTENER = 3,
  PLUG_STATUS_ERROR_OPERATION = 4,
};

typedef struct {
  uint32_t count;
  int32_t status;
  uint64_t gpa_base;
  uint64_t phys_addrs[0];
} __attribute__((packed)) plug_cmd_t;

class MemControllerImpl : public MemController, public IoHandler {
 public:
  MemControllerImpl(component::ServiceProviderBridge* bridge,
                    GuestConfig& cfg,
                    Guest* guest,
                    GicIts* gic_its);

  zx_status_t PatchDeviceTree(const DeviceTreeSpec& dtb, uint64_t phys_base);

 private:
  Guest* guest_;
  GicIts* gic_its_;
  uint64_t mmio_base_;
  uint64_t region_start_;
  std::string cma_compatible_;
  uint64_t uos_gpa_region_start_;
  uint64_t uos_gpa_region_size_;
  bool uos_gpa_region_ready_;
  plug_cmd_t* cmd_buf_;
  size_t cmd_buf_size_;

  enum Regs {
    REGION_START = 0x0,
    CMD_BUF = 0x8,
    CMD_BUF_SIZE = 0x10,
    UOS_GPA_REGION_START = 0x18,
    UOS_GPA_REGION_SIZE = 0x20,
    UOS_GPA_REGION_READY = 0x28,
    PLUG = 0x1000,
    UNPLUG = 0x1008,
  };

  void NotifyUosGpaRegion();
  void SendPlugMemoryRequest();
  void SendUnPlugMemoryRequest();

  void RegisterMemControllerListener(
      fidl::InterfaceHandle<MemControllerListener> listener,
      RegisterMemControllerListenerCallback callback) override;

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  fidl::BindingSet<MemController> bindings_;
  MemControllerListenerSyncPtr listener_;
};

}  // namespace machina
