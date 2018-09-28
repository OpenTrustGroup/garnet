// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_NAMESPACE_H_
#define GARNET_BIN_APPMGR_NAMESPACE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/bin/appmgr/job_provider_impl.h"
#include "garnet/bin/appmgr/service_provider_dir_impl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/strings/string_view.h"

namespace component {
class Realm;

class Namespace : public fuchsia::sys::Environment,
                  public fuchsia::sys::Launcher,
                  public fxl::RefCountedThreadSafe<Namespace> {
 public:
  const fbl::RefPtr<ServiceProviderDirImpl>& services() { return services_; }
  const fbl::RefPtr<JobProviderImpl>& job_provider() { return job_provider_; }

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::sys::Environment> environment);

  zx_status_t ServeServiceDirectory(zx::channel request);

  zx::channel OpenServicesAsDirectory();

  //
  // fuchsia::sys::Environment implementation:
  //

  void CreateNestedEnvironment(
      fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
      fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller,
      fidl::StringPtr label,
      fuchsia::sys::ServiceListPtr additional_services,
      fuchsia::sys::EnvironmentOptions options) override;


  void GetLauncher(
      fidl::InterfaceRequest<fuchsia::sys::Launcher> launcher) override;

  void GetServices(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) override;

  void GetDirectory(zx::channel directory_request) override {
    ServeServiceDirectory(std::move(directory_request));
  }

  void set_component_url(const std::string& url) {
    services_->set_component_url(url);
  }

  //
  // fuchsia::sys::Launcher implementation:
  //

  void CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                       fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                           controller) override;


 private:
  FRIEND_MAKE_REF_COUNTED(Namespace);
  Namespace(fxl::RefPtr<Namespace> parent, Realm* realm,
            fuchsia::sys::ServiceListPtr additional_services,
            const std::vector<std::string>* service_whitelist);

  FRIEND_REF_COUNTED_THREAD_SAFE(Namespace);
  ~Namespace() override;

  fidl::BindingSet<fuchsia::sys::Environment> environment_bindings_;
  fidl::BindingSet<fuchsia::sys::Launcher> launcher_bindings_;

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<ServiceProviderDirImpl> services_;
  fbl::RefPtr<JobProviderImpl> job_provider_;
  Realm* const realm_;
  // Set if |additional_services.provider| was set.
  fuchsia::sys::ServiceProviderPtr service_provider_;
  // Set if |additional_services.host_directory| was set.
  zx::channel service_host_directory_;
  fuchsia::sys::LoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Namespace);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_NAMESPACE_H_
