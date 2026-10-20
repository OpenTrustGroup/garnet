// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc_message.h"

namespace machina {

IpcMessage::IpcMessage(component::ServiceProviderBridge *bridge,
                       GuestConfig& cfg) {
  bridge->AddService<IpcMessageService>(
      [this](fidl::InterfaceRequest<IpcMessageService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  dtb_phys_addr_ = cfg.dtb().base;
  dtb_size_ = cfg.dtb().size;
  FXL_CHECK(loop_.StartThread() == ZX_OK);
}

void IpcMessage::GetTraceBuf(GetTraceBufCallback callback) {
  callback(trace_mem_pa_, trace_mem_sz_);
}

void IpcMessage::GetMonitorVirtioMem(GetMonitorVirtioMemCallback callback) {
  callback(monitor_virtio_mem_pa_, monitor_virtio_mem_sz_);
}

void IpcMessage::GetDeviceTree(GetDeviceTreeCallback callback) {
  callback(dtb_phys_addr_, dtb_size_);
}

void IpcMessage::GetCvmdBuf(GetCvmdBufCallback callback) {
  callback(cvmd_mem_pa_, cvmd_mem_sz_);
}

}  // namespace machina
