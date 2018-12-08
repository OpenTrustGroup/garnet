// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <gzos/ipc/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"

namespace gzos {

namespace ipc {

class Agent {
 public:
  Agent();

  template <typename INTERFACE>
  void ConnectToService(fidl::InterfaceRequest<INTERFACE> request) {
    std::string service_name = INTERFACE::Name_;
    service_provider_->ConnectToService(service_name, request.TakeChannel());
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
  async::Loop loop_;

  ServiceProviderPtr service_provider_;
};

}  // namespace ipc

}  // namespace gzos
