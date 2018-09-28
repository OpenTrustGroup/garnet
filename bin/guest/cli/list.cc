// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/list.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <iostream>

void handle_list(component::StartupContext* context) {
  fuchsia::guest::EnvironmentManagerSyncPtr environment_manager;
  context->ConnectToEnvironmentService(environment_manager.NewRequest());
  fidl::VectorPtr<fuchsia::guest::EnvironmentInfo> env_infos;
  environment_manager->List(&env_infos);
  if (env_infos->empty()) {
    printf("no environments\n");
  }
  for (const auto& env_info : *env_infos) {
    printf("env:%-4u          %s\n", env_info.id, env_info.label->c_str());
    if (env_info.instances->empty()) {
      printf(" no guest instances\n");
    }
    for (const auto& instance_info : *env_info.instances) {
      printf(" guest:%-4u       %s\n", instance_info.cid,
             instance_info.label->c_str());
    }
  }
}
