// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_APPLICATION_CONTEXT_H_
#define LIB_APP_CPP_APPLICATION_CONTEXT_H_

#include <fs/pseudo-dir.h>

#include <memory>

#include "lib/app/cpp/service_provider_impl.h"
#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/svc/cpp/service_namespace.h"

namespace component {

// Provides access to the application's environment and allows the application
// to publish outgoing services back to its creator.
class ApplicationContext {
 public:
  // The constructor is normally called by CreateFromStartupInfo().
  ApplicationContext(zx::channel service_root,
                     zx::channel directory_request);

  ~ApplicationContext();

  // Creates the application context from the process startup info.
  //
  // This function should be called once during process initialization to
  // retrieve the handles supplied to the application by the application
  // manager.
  //
  // This function will call FXL_CHECK and stack dump if the environment is
  // null. However, a null environment services pointer is allowed.
  //
  // The returned unique_ptr is never null.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfo();

  // Like CreateFromStartupInfo(), but allows both the environment and the
  // environment services to be null so that callers can validate the values
  // and provide meaningful error messages.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfoNotChecked();

  static std::unique_ptr<ApplicationContext> CreateFrom(
      ApplicationStartupInfo startup_info);

  // Gets the application's environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationEnvironmentPtr& environment() const { return environment_; }

  // Whether this application was given services by its environment.
  bool has_environment_services() const { return !!service_root_; }

  // Gets the application launcher service provided to the application by
  // its environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationLauncherPtr& launcher() const { return launcher_; }

  // Gets a service provider implementation by which the application can
  // provide outgoing services back to its creator.
  ServiceNamespace* outgoing_services() {
    return &deprecated_outgoing_services_;
  }

  // Gets the directory which is the root of the tree of file-system objects
  // exported by this application to the rest of the system.
  //
  // Clients should organize exported objects into sub-directories by role
  // using conventions such as the following:
  // - svc: public services exported by the application
  // - debug: debugging information exported by the application
  // - fs: the mounted file-system (for applications which are file-systems)
  const fbl::RefPtr<fs::PseudoDir>& export_dir() const {
    return export_dir_;
  }

  // Gets an export sub-directory called "public" for publishing services for
  // clients.
  const fbl::RefPtr<fs::PseudoDir>& public_export_dir() const {
    return public_export_dir_;
  }

  // Gets an export sub-directory called "debug" for publishing debugging
  // information.
  const fbl::RefPtr<fs::PseudoDir>& debug_export_dir() const {
    return debug_export_dir_;
  }

  // Gets an export sub-directory called "ctrl" for publishing services for
  // appmgr.
  const fbl::RefPtr<fs::PseudoDir>& ctrl_export_dir() const {
    return ctrl_export_dir_;
  }


  // Connects to a service provided by the application's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> interface_ptr;
    ConnectToEnvironmentService(interface_name,
                                interface_ptr.NewRequest().TakeChannel());
    return interface_ptr;
  }

  // Connects to a service provided by the application's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void ConnectToEnvironmentService(
      fidl::InterfaceRequest<Interface> request,
      const std::string& interface_name = Interface::Name_) {
    ConnectToEnvironmentService(interface_name, request.TakeChannel());
  }

  // Connects to a service provided by the application's environment,
  // binding the service to a channel.
  void ConnectToEnvironmentService(const std::string& interface_name,
                                   zx::channel channel);

 private:
  ApplicationEnvironmentPtr environment_;
  ApplicationLauncherPtr launcher_;

  fs::ManagedVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> export_dir_;
  fbl::RefPtr<fs::PseudoDir> public_export_dir_;
  fbl::RefPtr<fs::PseudoDir> debug_export_dir_;
  fbl::RefPtr<fs::PseudoDir> ctrl_export_dir_;

  zx::channel service_root_;

  ServiceNamespace deprecated_outgoing_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationContext);
};

}  // namespace component

#endif  // LIB_APP_CPP_APPLICATION_CONTEXT_H_
