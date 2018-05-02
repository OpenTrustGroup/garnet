// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_controller_impl.h"

#include <fbl/string_printf.h>
#include <fdio/util.h>
#include <fs/pseudo-file.h>
#include <fs/remote-dir.h>
#include <lib/async/default.h>

#include <cinttypes>
#include <utility>

#include "garnet/bin/appmgr/application_namespace.h"
#include "garnet/bin/appmgr/job_holder.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/closure.h"

namespace component {

ApplicationControllerImpl::ApplicationControllerImpl(
    fidl::InterfaceRequest<ApplicationController> request,
    JobHolder* job_holder,
    std::unique_ptr<archive::FileSystem> fs,
    zx::process process,
    std::string url,
    std::string label,
    fxl::RefPtr<ApplicationNamespace> application_namespace,
    ExportedDirType export_dir_type,
    zx::channel exported_dir,
    zx::channel client_request)
    : binding_(this),
      job_holder_(job_holder),
      fs_(std::move(fs)),
      process_(std::move(process)),
      label_(std::move(label)),
      info_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      exported_dir_(std::move(exported_dir)),
      application_namespace_(std::move(application_namespace)),
      wait_(this, process_.get(), ZX_TASK_TERMINATED) {
  zx_status_t status = wait_.Begin(async_get_default());
  FXL_DCHECK(status == ZX_OK);
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this] { Kill(); });
  }

  if (!exported_dir_) {
    return;
  }

  if (client_request) {
    if (export_dir_type == ExportedDirType::kPublicDebugCtrlLayout) {
      fdio_service_connect_at(exported_dir_.get(), "public",
                              client_request.release());
    } else if (export_dir_type == ExportedDirType::kLegacyFlatLayout) {
      fdio_service_clone_to(exported_dir_.get(), client_request.release());
    }
  }
  if (export_dir_type == ExportedDirType::kPublicDebugCtrlLayout) {
    zx::channel debug_dir_server, debug_dir_client;
    zx_status_t status =
        zx::channel::create(0u, &debug_dir_server, &debug_dir_client);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create channel for service directory."
                     << status;
      return;
    }
    fdio_service_connect_at(exported_dir_.get(), "debug",
                            debug_dir_server.release());
    zx_koid_t process_koid = fsl::GetKoid(process_.get());
    info_dir_->AddEntry("process", fbl::AdoptRef(new fs::UnbufferedPseudoFile(
                                       [process_koid](fbl::String* output) {
                                         *output = fbl::StringPrintf(
                                             "%" PRIu64, process_koid);
                                         return ZX_OK;
                                       })));
    info_dir_->AddEntry(
        "url",
        fbl::AdoptRef(new fs::UnbufferedPseudoFile([url](fbl::String* output) {
          *output = url;
          return ZX_OK;
        })));
    info_dir_->AddEntry(
        "debug", fbl::AdoptRef(new fs::RemoteDir(fbl::move(debug_dir_client))));
  }
}

ApplicationControllerImpl::~ApplicationControllerImpl() {
  // Two ways we end up here:
  // 1) OnHandleReady() destroys this object; in which case, process is dead.
  // 2) Our owner destroys this object; in which case, the process may still be
  //    alive.
  if (process_)
    process_.kill();
}

void ApplicationControllerImpl::Kill() {
  process_.kill();
}

void ApplicationControllerImpl::Detach() {
  binding_.set_error_handler(fxl::Closure());
}

bool ApplicationControllerImpl::SendReturnCodeIfTerminated() {
  // Get process info.
  zx_info_process_t process_info;
  zx_status_t result = process_.get_info(ZX_INFO_PROCESS, &process_info,
                                         sizeof(process_info), NULL, NULL);
  FXL_DCHECK(result == ZX_OK);

  if (process_info.exited) {
    // If the process has exited, call the callbacks.
    for (const auto& iter : wait_callbacks_) {
      iter(process_info.return_code);
    }
    wait_callbacks_.clear();
  }

  return process_info.exited;
}

void ApplicationControllerImpl::Wait(WaitCallback callback) {
  wait_callbacks_.push_back(callback);
  SendReturnCodeIfTerminated();
}

// Called when process terminates, regardless of if Kill() was invoked.
void ApplicationControllerImpl::Handler(
    async_t* async,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal* signal) {
  FXL_DCHECK(status == ZX_OK);
  FXL_DCHECK(signal->observed == ZX_TASK_TERMINATED);
  if (!wait_callbacks_.empty()) {
    bool terminated = SendReturnCodeIfTerminated();
    FXL_DCHECK(terminated);
  }

  process_.reset();

  job_holder_->ExtractApplication(this);
  // The destructor of the temporary returned by ExtractApplication destroys
  // |this| at the end of the previous statement.
}

}  // namespace component
