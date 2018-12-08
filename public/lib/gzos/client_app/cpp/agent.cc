// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "agent.h"

#include "lib/fxl/logging.h"
#include "lib/async/default.h"

namespace gzos {

namespace ipc {

Agent::Agent()
    : context_(component::StartupContext::CreateFromStartupInfo()),
      loop_(&kAsyncLoopConfigAttachToThread) {
  context_->ConnectToEnvironmentService<gzos::ipc::ServiceProvider>(
      service_provider_.NewRequest());

  service_provider_.set_error_handler([this]() {
    FXL_LOG(FATAL)
        << "Exiting due to not able to connect to gz_ipc ServiceProvider";
  });

  loop_.StartThread();
}

}  // namespace ipc

}  // namespace gzos
