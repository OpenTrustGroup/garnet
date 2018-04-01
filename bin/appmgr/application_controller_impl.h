// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_

#include <fs/pseudo-dir.h>
#include <lib/async/cpp/auto_wait.h>
#include <zx/process.h>

#include "garnet/bin/appmgr/application_namespace.h"
#include "garnet/lib/farfs/file_system.h"
#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace component {
class JobHolder;

enum class ExportedDirType {
  // Legacy exported directory layout where each file / service is exposed at
  // the top level. Appmgr forwards a client's
  // |ApplicationLaunchInfo.directory_request| to the top level directory.
  kLegacyFlatLayout,

  // A nested directory structure where appmgr expects 3 sub-directories-
  // (1) public - A client's |ApplicationLaunchInfo.directory_request| is
  // forwarded to this directory.
  // (2) debug - This directory is used to expose debug files.
  // (3) ctrl - This deirectory is used to expose files to the system.
  kPublicDebugCtrlLayout,
};

class ApplicationControllerImpl : public ApplicationController {
 public:
  ApplicationControllerImpl(
      fidl::InterfaceRequest<ApplicationController> request,
      JobHolder* job_holder,
      std::unique_ptr<archive::FileSystem> fs,
      zx::process process,
      std::string url,
      std::string label,
      fxl::RefPtr<ApplicationNamespace> application_namespace,
      ExportedDirType export_dir_type,
      zx::channel exported_dir,
      zx::channel client_request);
  ~ApplicationControllerImpl() override;

  const std::string& label() const { return label_; }
  const fbl::RefPtr<fs::PseudoDir>& info_dir() const { return info_dir_; }

  // |ApplicationController| implementation:
  void Kill() override;
  void Detach() override;
  void Wait(WaitCallback callback) override;

 private:
  async_wait_result_t Handler(async_t* async,
                              zx_status_t status,
                              const zx_packet_signal* signal);

  bool SendReturnCodeIfTerminated();

  fidl::Binding<ApplicationController> binding_;
  JobHolder* job_holder_;
  std::unique_ptr<archive::FileSystem> fs_;
  zx::process process_;
  std::string label_;
  std::vector<WaitCallback> wait_callbacks_;
  fbl::RefPtr<fs::PseudoDir> info_dir_;

  zx::channel exported_dir_;

  fxl::RefPtr<ApplicationNamespace> application_namespace_;

  async::AutoWait wait_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationControllerImpl);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_
