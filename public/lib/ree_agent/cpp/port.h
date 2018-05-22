// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fuchsia/cpp/ree_agent.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"

namespace ree_agent {

class TipcPortImpl : public TipcPort {
 public:
  using ConnectRequestCallback =
      std::function<void(fidl::InterfaceRequest<TipcChannel>)>;

  TipcPortImpl(component::Services* services,
               const fbl::String path,
               ConnectRequestCallback callback)
      : binding_(this), path_(path.c_str()), callback_(callback) {
    services->ConnectToService<TipcPortManager>(port_mgr_.NewRequest());
  }
  TipcPortImpl() = delete;

  void Publish(TipcPortManager::PublishCallback callback) {
    fidl::InterfaceHandle<TipcPort> handle;
    binding_.Bind(handle.NewRequest());
    port_mgr_->Publish(path_, std::move(handle), callback);
  }

  ~TipcPortImpl() {
    // TODO(sy): unregister port here
  }

 private:
  void OnConnectionRequest(
      fidl::InterfaceRequest<TipcChannel> channel) override;

  fidl::Binding<TipcPort> binding_;
  fidl::StringPtr path_;
  ConnectRequestCallback callback_;
  TipcPortManagerPtr port_mgr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcPortImpl);
};

}  // namespace ree_agent
