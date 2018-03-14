// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_APP_H_
#define GARNET_BIN_SYSMGR_APP_H_

#include <map>
#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/svc/cpp/service_namespace.h"
#include "lib/svc/cpp/services.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "garnet/bin/sysmgr/delegating_application_loader.h"
#include "lib/fxl/macros.h"

namespace sysmgr {

// The sysmgr creates a nested environment within which it starts apps
// and wires up the UI services they require.
//
// The nested environment consists of the following system applications
// which are started on demand then retained as singletons for the lifetime
// of the environment.
class App : public app::ApplicationEnvironmentHost {
 public:
  App();
  ~App();

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      f1dl::InterfaceRequest<app::ServiceProvider> environment_services)
      override;

  void RegisterSingleton(std::string service_name,
                         app::ApplicationLaunchInfoPtr launch_info);
  void RegisterDefaultServiceConnector();
  void RegisterAppLoaders(Config::ServiceMap app_loaders);
  void LaunchApplication(app::ApplicationLaunchInfoPtr launch_info);

  std::unique_ptr<app::ApplicationContext> application_context_;

  // Keep track of all services, indexed by url.
  std::map<std::string, app::Services> services_;

  // Nested environment within which the apps started by sysmgr will run.
  app::ApplicationEnvironmentPtr env_;
  app::ApplicationEnvironmentControllerPtr env_controller_;
  f1dl::Binding<app::ApplicationEnvironmentHost> env_host_binding_;
  app::ServiceNamespace env_services_;
  app::ApplicationLauncherPtr env_launcher_;

  std::unique_ptr<DelegatingApplicationLoader> app_loader_;
  f1dl::BindingSet<app::ApplicationLoader> app_loader_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_APP_H_
