// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_REALM_H_
#define GARNET_BIN_APPMGR_REALM_H_

#include <fs/synchronous-vfs.h>
#include <zx/channel.h>

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/bin/appmgr/component_container.h"
#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/environment_controller_impl.h"
#include "garnet/bin/appmgr/hub/hub_info.h"
#include "garnet/bin/appmgr/hub/realm_hub.h"
#include "garnet/bin/appmgr/namespace.h"
#include "garnet/bin/appmgr/namespace_builder.h"
#include "garnet/bin/appmgr/root_loader.h"
#include "garnet/bin/appmgr/runner_holder.h"
#include "garnet/bin/appmgr/scheme_map.h"
#include "garnet/lib/cmx/runtime.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fit/function.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace component {

struct RealmArgs {
  static RealmArgs Make(
      Realm* parent, fidl::StringPtr label,
      const std::shared_ptr<component::Services>& env_services,
      bool run_virtual_console, bool inherit_parent_services);

 static RealmArgs MakeWithAdditionalServices(
      Realm* parent, fidl::StringPtr label,
      const std::shared_ptr<component::Services>& env_services,
      bool run_virtual_console,
      fuchsia::sys::ServiceListPtr additional_services,
      bool inherit_parent_services);

  Realm* parent;
  fidl::StringPtr label;
  std::shared_ptr<component::Services> environment_services;
  bool run_virtual_console;
  fuchsia::sys::ServiceListPtr additional_services;
  bool inherit_parent_services;
  bool allow_parent_runners;
};

class Realm : public ComponentContainer<ComponentControllerImpl> {
 public:
  Realm(RealmArgs args);
  ~Realm();

  Realm* parent() const { return parent_; }
  const std::string& label() const { return label_; }
  const std::string& koid() const { return koid_; }

  const fbl::RefPtr<fs::PseudoDir>& hub_dir() const { return hub_.dir(); }

  std::shared_ptr<component::Services> environment_services() const {
    return environment_services_;
  }

  HubInfo HubInfo();

  zx::job DuplicateJob() const;

  void CreateNestedEnvironment(
      fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
      fidl::InterfaceRequest<fuchsia::sys::EnvironmentController>
          controller_request,
      fidl::StringPtr label, fuchsia::sys::ServiceListPtr additional_services,
      bool inherit_parent_services, bool allow_parent_runners);

  using ComponentObjectCreatedCallback =
      fit::function<void(ComponentControllerImpl* component)>;

  void CreateComponent(
      fuchsia::sys::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
      ComponentObjectCreatedCallback callback = nullptr);

  // Removes the child realm from this realm and returns the owning
  // reference to the child's controller. The caller of this function typically
  // destroys the controller (and hence the environment) shortly after calling
  // this function.
  std::unique_ptr<EnvironmentControllerImpl> ExtractChild(Realm* child);

  // Removes the application from this environment and returns the owning
  // reference to the application's controller. The caller of this function
  // typically destroys the controller (and hence the application) shortly after
  // calling this function.
  std::unique_ptr<ComponentControllerImpl> ExtractComponent(
      ComponentControllerImpl* controller) override;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::sys::Environment> environment);

  zx_status_t BindSvc(zx::channel channel);
  void CreateShell(const std::string& path, zx::channel svc);

 private:
  static uint32_t next_numbered_label_;

  RunnerHolder* GetOrCreateRunner(const std::string& runner);
  RunnerHolder* GetRunnerRecursive(const std::string& runner) const;

  void CreateComponentWithRunnerForScheme(
      std::string runner_url, fuchsia::sys::LaunchInfo launch_info,
      ComponentRequestWrapper component_request,
      ComponentObjectCreatedCallback callback);

  void CreateComponentWithProcess(fuchsia::sys::PackagePtr package,
                                  fuchsia::sys::LaunchInfo launch_info,
                                  ComponentRequestWrapper component_request,
                                  ComponentObjectCreatedCallback callback);

  void CreateComponentFromPackage(fuchsia::sys::PackagePtr package,
                                  fuchsia::sys::LaunchInfo launch_info,
                                  ComponentRequestWrapper component_request,
                                  ComponentObjectCreatedCallback callback);

  void CreateElfBinaryComponentFromPackage(
      fuchsia::sys::LaunchInfo launch_info, fsl::SizedVmo& app_data,
      const std::string& app_argv0, ExportedDirType exported_dir_layout,
      zx::channel loader_service, fdio_flat_namespace_t* flat,
      ComponentRequestWrapper component_request, fxl::RefPtr<Namespace> ns,
      ComponentObjectCreatedCallback callback);

  void CreateRunnerComponentFromPackage(
      fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
      RuntimeMetadata& runtime, fuchsia::sys::FlatNamespace flat,
      ComponentRequestWrapper component_request, fxl::RefPtr<Namespace> ns,
      fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata);

  zx::channel OpenInfoDir();

  Realm* const parent_;
  fuchsia::sys::LoaderPtr loader_;
  std::string label_;
  std::string koid_;
  const bool run_virtual_console_;
  std::unique_ptr<RootLoader> root_loader_;

  zx::job job_;

  fxl::RefPtr<Namespace> default_namespace_;

  RealmHub hub_;
  fs::SynchronousVfs info_vfs_;

  std::unordered_map<Realm*, std::unique_ptr<EnvironmentControllerImpl>>
      children_;

  std::unordered_map<ComponentControllerImpl*,
                     std::unique_ptr<ComponentControllerImpl>>
      applications_;

  std::unordered_map<std::string, std::unique_ptr<RunnerHolder>> runners_;

  zx::channel svc_channel_client_;
  zx::channel svc_channel_server_;

  SchemeMap scheme_map_;

  const std::shared_ptr<component::Services> environment_services_;

  bool allow_parent_runners_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Realm);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_REALM_H_
