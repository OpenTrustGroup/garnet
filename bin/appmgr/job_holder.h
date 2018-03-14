// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_JOB_HOLDER_H_
#define GARNET_BIN_APPMGR_JOB_HOLDER_H_

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include <fs/pseudo-dir.h>
#include <fs/vfs.h>

#include "garnet/bin/appmgr/application_controller_impl.h"
#include "garnet/bin/appmgr/application_environment_controller_impl.h"
#include "garnet/bin/appmgr/application_namespace.h"
#include "garnet/bin/appmgr/application_runner_holder.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/app/fidl/application_loader.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace app {

class NamespaceBuilder;

class JobHolder {
 public:
  JobHolder(JobHolder* parent,
            fs::Vfs* vfs,
            f1dl::InterfaceHandle<ApplicationEnvironmentHost> host,
            const f1dl::String& label);
  ~JobHolder();

  JobHolder* parent() const { return parent_; }
  const std::string& label() const { return label_; }

  const fbl::RefPtr<fs::PseudoDir>& info_dir() const { return info_dir_; }

  void CreateNestedJob(
      f1dl::InterfaceHandle<ApplicationEnvironmentHost> host,
      f1dl::InterfaceRequest<ApplicationEnvironment> environment,
      f1dl::InterfaceRequest<ApplicationEnvironmentController> controller,
      const f1dl::String& label);

  void CreateApplication(
      ApplicationLaunchInfoPtr launch_info,
      f1dl::InterfaceRequest<ApplicationController> controller);

  // Removes the child job holder from this job holder and returns the owning
  // reference to the child's controller. The caller of this function typically
  // destroys the controller (and hence the environment) shortly after calling
  // this function.
  std::unique_ptr<ApplicationEnvironmentControllerImpl> ExtractChild(
      JobHolder* child);

  // Removes the application from this environment and returns the owning
  // reference to the application's controller. The caller of this function
  // typically destroys the controller (and hence the application) shortly after
  // calling this function.
  std::unique_ptr<ApplicationControllerImpl> ExtractApplication(
      ApplicationControllerImpl* controller);

  void AddBinding(f1dl::InterfaceRequest<ApplicationEnvironment> environment);

 private:
  static uint32_t next_numbered_label_;

  ApplicationRunnerHolder* GetOrCreateRunner(const std::string& runner);

  void CreateApplicationWithProcess(
      ApplicationPackagePtr package,
      ApplicationLaunchInfoPtr launch_info,
      f1dl::InterfaceRequest<ApplicationController> controller,
      fxl::RefPtr<ApplicationNamespace> application_namespace);
  void CreateApplicationFromPackage(
      ApplicationPackagePtr package,
      ApplicationLaunchInfoPtr launch_info,
      f1dl::InterfaceRequest<ApplicationController> controller,
      fxl::RefPtr<ApplicationNamespace> application_namespace);

  // TODO(ZX-1036): For scaffolding purposes, we make the information available
  // to all applications started within the scope of the job.  Once we have
  // the means to do so, we should lock this down to prevent undesirable
  // information leakage.
  void AddInfoDir(NamespaceBuilder* builder);

  JobHolder* const parent_;
  fs::Vfs* const vfs_;
  ApplicationEnvironmentHostPtr host_;
  ApplicationLoaderPtr loader_;
  std::string label_;

  zx::job job_;
  zx::job job_for_child_;

  fxl::RefPtr<ApplicationNamespace> default_namespace_;

  // A pseudo-directory which describes the components within the scope of
  // this job.
  fbl::RefPtr<fs::PseudoDir> info_dir_;

  std::unordered_map<JobHolder*,
                     std::unique_ptr<ApplicationEnvironmentControllerImpl>>
      children_;

  std::unordered_map<ApplicationControllerImpl*,
                     std::unique_ptr<ApplicationControllerImpl>>
      applications_;

  std::unordered_map<std::string, std::unique_ptr<ApplicationRunnerHolder>>
      runners_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JobHolder);
};

}  // namespace app

#endif  // GARNET_BIN_APPMGR_JOB_HOLDER_H_
