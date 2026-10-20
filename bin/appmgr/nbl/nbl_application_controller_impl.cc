// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/nbl/nbl_application_controller_impl.h"

#include "garnet/bin/appmgr/job_holder.h"

namespace component {

NblApplicationControllerImpl::NblApplicationControllerImpl(
    fidl::InterfaceRequest<ApplicationController> request,
    JobHolder* job_holder,
    int32_t launch_res)
    : binding_(this), job_holder_(job_holder), launch_res_(launch_res) {
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
  }
}

void NblApplicationControllerImpl::WaitLaunchResponse(
    WaitLaunchResponseCallback callback) {
#ifdef __Nebula__
  callback(launch_res_);

  job_holder_->RemoveNblApplication(this);
#endif
}

}  // namespace component