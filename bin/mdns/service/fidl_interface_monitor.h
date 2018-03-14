// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/mdns/service/interface_monitor.h"
#include "lib/app/cpp/application_context.h"
#include "lib/netstack/fidl/netstack.fidl.h"

namespace mdns {

// FIDL-based interface monitor implementation.
class FidlInterfaceMonitor : public netstack::NotificationListener,
                             public InterfaceMonitor {
 public:
  static std::unique_ptr<InterfaceMonitor> Create(
      app::ApplicationContext* application_context);

  FidlInterfaceMonitor(app::ApplicationContext* application_context);

  ~FidlInterfaceMonitor();

  // InterfaceMonitor implementation.
  void RegisterLinkChangeCallback(const fxl::Closure& callback) override;

  const std::vector<std::unique_ptr<InterfaceDescriptor>>& GetInterfaces()
      override;

 private:
  // NotificationListener implementation.
  void OnInterfacesChanged(
      f1dl::Array<netstack::NetInterfacePtr> interfaces) override;

  netstack::NetstackPtr netstack_;
  f1dl::Binding<netstack::NotificationListener> binding_;
  fxl::Closure link_change_callback_;
  std::vector<std::unique_ptr<InterfaceDescriptor>> interfaces_;
};

}  // namespace mdns
