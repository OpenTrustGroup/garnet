// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_STANDALONE_IOCTL_INTERFACE_MONITOR_H_
#define GARNET_BIN_MDNS_STANDALONE_IOCTL_INTERFACE_MONITOR_H_

#include <memory>

#include "garnet/bin/mdns/service/interface_monitor.h"
#include "garnet/public/lib/fxl/functional/cancelable_callback.h"

namespace mdns {

// IOCTL-based interface monitor implementation.
class IoctlInterfaceMonitor : public InterfaceMonitor {
 public:
  static std::unique_ptr<InterfaceMonitor> Create();

  IoctlInterfaceMonitor();

  ~IoctlInterfaceMonitor();

  // InterfaceMonitor implementation.
  void RegisterLinkChangeCallback(fit::closure callback) override;

  const std::vector<std::unique_ptr<InterfaceDescriptor>>& GetInterfaces()
      override;

 private:
  // Calls |CheckInterfaces|, calling |link_change_callback_|
  // when a link change is detected, and schedules a delayed call to itself.
  void Poll();

  // Checks the interface list for changes. Returns true if and only if the
  // interfaces should be checked again soon.
  bool CheckInterfaces();

  fit::closure link_change_callback_;
  std::vector<std::unique_ptr<InterfaceDescriptor>> interfaces_;
  fxl::CancelableClosure poll_closure_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_STANDALONE_IOCTL_INTERFACE_MONITOR_H_
