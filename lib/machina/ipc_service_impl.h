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
#include "garnet/lib/machina/ipc_channel.h"
#include "lib/app/cpp/application_context.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class IpcServiceImpl : public IpcService, public IpcRequestHandler {
 public:
  IpcServiceImpl(component::ServiceProviderBridge* bridge,
                 Guest* guest,
                 const std::vector<machina::IpcMboxSpec>& ipcmbox);

  // |IpcService|
  virtual void RegisterIpcEventListener(
      uint16_t vmid,
      fidl::InterfaceHandle<IpcEventListener> listener,
      RegisterIpcEventListenerCallback callback) override;
  virtual void RegisterIpcEnumerator(
      uint16_t vmid,
      fidl::InterfaceHandle<IpcEnumerator> listener,
      RegisterIpcEnumeratorCallback callback) override;
  virtual void GetIpcEventListener(
      uint16_t vmid,
      GetIpcEventListenerCallback callback) override;
  virtual void EnumerateIpcChannels(
      uint16_t vmid,
      EnumerateIpcChannelsCallback callback) override;

  virtual void GetIrqInfo(uint16_t vmid, GetIrqInfoCallback callback) override;
  virtual void SetupFastCallChannel(
      uint16_t vmid,
      SetupFastCallChannelCallback callback) override;
  virtual void SetupVMCallChannel(uint16_t vmid,
                                  SetupVMCallChannelCallback callback) override;
  virtual void GetSchedBuf(uint16_t vmid,
                           GetSchedBufCallback callback) override;
  std::map<uint16_t, std::unique_ptr<IpcRequestHandler>>& getIpchandlerMap(
      void) {
    return ipchandlermap_;
  };

 private:
  fidl::BindingSet<IpcService> bindings_;
  std::vector<std::unique_ptr<IpcEnumeratorSyncPtr>> enumeratorsync_;
  std::map<uint16_t, std::unique_ptr<IpcRequestHandler>> ipchandlermap_;
  std::map<uint16_t, std::unique_ptr<IpcEnumeratorSyncPtr>> enumeratormap_;
  std::vector<machina::IpcMboxSpec> ipcmbox_;
  async::Loop loop_;
};

}  // namespace machina
