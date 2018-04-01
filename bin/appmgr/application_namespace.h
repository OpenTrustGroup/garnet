// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPLICATION_NAMESPACE_H_
#define GARNET_BIN_APPMGR_APPLICATION_NAMESPACE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace component {
class JobHolder;

class ApplicationNamespace
    : public ApplicationEnvironment,
      public ApplicationLauncher,
      public fxl::RefCountedThreadSafe<ApplicationNamespace> {
 public:
  ServiceProviderBridge& services() { return services_; }

  void AddBinding(fidl::InterfaceRequest<ApplicationEnvironment> environment);

  // ApplicationEnvironment implementation:

  void CreateNestedEnvironment(
      zx::channel host_directory,
      fidl::InterfaceRequest<ApplicationEnvironment> environment,
      fidl::InterfaceRequest<ApplicationEnvironmentController> controller,
      fidl::StringPtr label) override;

  void GetApplicationLauncher(
      fidl::InterfaceRequest<ApplicationLauncher> launcher) override;

  void GetServices(fidl::InterfaceRequest<ServiceProvider> services) override;

  void GetDirectory(zx::channel directory_request) override;

  // ApplicationLauncher implementation:

  void CreateApplication(
      ApplicationLaunchInfo launch_info,
      fidl::InterfaceRequest<ApplicationController> controller) override;

 private:
  FRIEND_MAKE_REF_COUNTED(ApplicationNamespace);
  ApplicationNamespace(fxl::RefPtr<ApplicationNamespace> parent,
                       JobHolder* job_holder,
                       ServiceListPtr service_list);

  FRIEND_REF_COUNTED_THREAD_SAFE(ApplicationNamespace);
  ~ApplicationNamespace() override;

  fidl::BindingSet<ApplicationEnvironment> environment_bindings_;
  fidl::BindingSet<ApplicationLauncher> launcher_bindings_;

  ServiceProviderBridge services_;

  fxl::RefPtr<ApplicationNamespace> parent_;
  JobHolder* job_holder_;
  ServiceProviderPtr additional_services_;
  ApplicationLoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationNamespace);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_APPLICATION_NAMESPACE_H_
