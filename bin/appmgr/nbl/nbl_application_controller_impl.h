// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_GRT_FAKE_APPLICATION_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_GRT_FAKE_APPLICATION_CONTROLLER_IMPL_H_

#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding.h"

#include "garnet/bin/appmgr/application_namespace.h"

namespace component {

/**
 * This application controller impl is only used to send message to nbl loader.
 */
class NblApplicationControllerImpl : public ApplicationController {
 public:
  NblApplicationControllerImpl(
      fidl::InterfaceRequest<ApplicationController> request,
      JobHolder* job_holder,
      int32_t launch_res);
  void Kill() override{};
  void KillAndWait(KillAndWaitCallback callback) override{};
  void Detach() override{};
  void Wait(WaitCallback callback) override{};
  void WaitLaunchResponse(WaitLaunchResponseCallback callback) override;
  ~NblApplicationControllerImpl() override{};

 private:
  fidl::Binding<ApplicationController> binding_;
  JobHolder* job_holder_;
  int32_t launch_res_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NblApplicationControllerImpl);
};
} // namespace component

#endif