// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_STARTUP_CONTEXT_H_
#define LIB_COMPONENT_CPP_STARTUP_CONTEXT_H_

#include <memory>
#include <mutex>

#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/compiler.h>
#include "lib/component/cpp/outgoing.h"
#include "lib/component/cpp/service_provider_impl.h"
#include "lib/svc/cpp/services.h"

namespace component {

// Provides access to the component's environment and allows the component
// to publish outgoing services back to its creator.
class StartupContext {
 public:
  // The constructor is normally called by CreateFromStartupInfo().
  StartupContext(zx::channel service_root, zx::channel directory_request);

  ~StartupContext();

  StartupContext(const StartupContext&) = delete;
  StartupContext& operator=(const StartupContext&) = delete;

  // Creates the component context from the process startup info.
  //
  // This function should be called once during process initialization to
  // retrieve the handles supplied to the component by the component
  // manager.
  //
  // This function will call FXL_CHECK and stack dump if the environment is
  // null. However, a null environment services pointer is allowed.
  //
  // The returned unique_ptr is never null.
  static std::unique_ptr<StartupContext> CreateFromStartupInfo();

  // DEPRECATED: Same as CreateFromStartupInfo().
  //
  // TODO(geb): Remove this method once users in higher layers have been
  // converted, it's obsolete.
  static std::unique_ptr<StartupContext> CreateFromStartupInfoNotChecked();

  static std::unique_ptr<StartupContext> CreateFrom(
      fuchsia::sys::StartupInfo startup_info);

  // Gets the component's environment.
  //
  // May be null if the component does not have access to its environment.
  const fuchsia::sys::EnvironmentPtr& environment() const;

  // Whether this component was given services by its environment.
  bool has_environment_services() const {
    return !!incoming_services()->directory();
  }

  // Gets the component launcher service provided to the component by
  // its environment.
  //
  // May be null if the component does not have access to its environment.
  const fuchsia::sys::LauncherPtr& launcher() const;

  const std::shared_ptr<Services>& incoming_services() const {
    return incoming_services_;
  }
  const Outgoing& outgoing() const { return outgoing_; }

  // Gets a service provider implementation by which the component can
  // provide outgoing services back to its creator.
  ServiceNamespace* outgoing_services() {
    return outgoing().deprecated_services();
  }

  // Connects to a service provided by the component's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    return incoming_services()->ConnectToService<Interface>(interface_name);
  }

  // Connects to a service provided by the component's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void ConnectToEnvironmentService(
      fidl::InterfaceRequest<Interface> request,
      const std::string& interface_name = Interface::Name_) {
    return incoming_services()->ConnectToService(std::move(request),
                                                 interface_name);
  }

  // Connects to a service provided by the component's environment,
  // binding the service to a channel.
  void ConnectToEnvironmentService(const std::string& interface_name,
                                   zx::channel channel);

 private:
  std::shared_ptr<Services> incoming_services_;
  Outgoing outgoing_;
  mutable std::mutex services_mutex_;
  mutable fuchsia::sys::EnvironmentPtr environment_
      __TA_GUARDED(services_mutex_);
  mutable fuchsia::sys::LauncherPtr launcher_ __TA_GUARDED(services_mutex_);
};

namespace subtle {

// This returns creates a new channel connected to the application's static
// environment service provider.
zx::channel CreateStaticServiceRootHandle();

}  // namespace subtle

}  // namespace component

#endif  // LIB_COMPONENT_CPP_STARTUP_CONTEXT_H_
