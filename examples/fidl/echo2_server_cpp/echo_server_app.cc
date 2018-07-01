// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_server_app.h"

#include "lib/app/cpp/startup_context.h"

namespace echo2 {

EchoServerApp::EchoServerApp()
    : EchoServerApp(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {}

EchoServerApp::EchoServerApp(
    std::unique_ptr<fuchsia::sys::StartupContext> context)
    : context_(std::move(context)) {
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void EchoServerApp::EchoString(fidl::StringPtr value,
                               EchoStringCallback callback) {
  printf("EchoString: %s\n", value->data());
  callback(std::move(value));
}

}  // namespace echo2
