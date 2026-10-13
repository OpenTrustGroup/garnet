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

#include <fbl/mutex.h>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "lib/async/cpp/wait.h"

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
  ~MemControllerImpl();

  zx_status_t PatchDeviceTree(const DeviceTreeSpec& dtb, uint64_t phys_base);

 private:
  Guest* guest_;
  GicIts* gic_its_;
  uint64_t mmio_base_;
  std::string cma_compatible_;
  // Serializes region/cmd_buf/listener state across VCPU MMIO, trapbell,
  // FIDL register, and listener-closed. Held across sync listener RPCs.
  fbl::Mutex mutex_;
  uint64_t uos_gpa_region_start_ __TA_GUARDED(mutex_);
  uint64_t uos_gpa_region_size_ __TA_GUARDED(mutex_);
  bool uos_gpa_region_ready_ __TA_GUARDED(mutex_);
  // Plug/unplug granularity, configured by the SOS backend via Regs::BLOCK_SIZE.
  uint64_t block_size_ __TA_GUARDED(mutex_);
  plug_cmd_t* cmd_buf_ __TA_GUARDED(mutex_);
  size_t cmd_buf_size_ __TA_GUARDED(mutex_);

  enum Regs {
    /* 0x0 was REGION_START; unused, keep later offsets stable. */
    CMD_BUF = 0x8,
    CMD_BUF_SIZE = 0x10,
    UOS_GPA_REGION_START = 0x18,
    UOS_GPA_REGION_SIZE = 0x20,
    UOS_GPA_REGION_READY = 0x28,
    BLOCK_SIZE = 0x30,
    PLUG = 0x1000,
    UNPLUG = 0x1008,
  };

  void NotifyUosGpaRegion() __TA_REQUIRES(mutex_);
  void SendPlugMemoryRequest() __TA_REQUIRES(mutex_);
  void SendUnPlugMemoryRequest() __TA_REQUIRES(mutex_);
  void ResetListenerLocked() __TA_REQUIRES(mutex_);
  void DropListener();
  async_wait_result_t OnListenerClosed(async_t* async,
                                       zx_status_t status,
                                       const zx_packet_signal_t* signal);

  void RegisterMemControllerListener(
      fidl::InterfaceHandle<MemControllerListener> listener,
      RegisterMemControllerListenerCallback callback) override;

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  fidl::BindingSet<MemController> bindings_;
  MemControllerListenerSyncPtr listener_ __TA_GUARDED(mutex_);
  async::WaitMethod<MemControllerImpl, &MemControllerImpl::OnListenerClosed>
      listener_waiter_{this};
};

}  // namespace machina
