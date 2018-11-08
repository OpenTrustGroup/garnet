// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gzos/samples/echo/cpp/fidl.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include "lib/fidl/cpp/binding_set.h"

class EchoServiceImpl : public gzos::samples::echo::EchoService {
 public:
  EchoServiceImpl()
      : context_(component::StartupContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

 private:
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(value);
  }

  fidl::BindingSet<gzos::samples::echo::EchoService> bindings_;
  std::unique_ptr<component::StartupContext> context_;
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  EchoServiceImpl echo_server;

  loop.Run();
  return 0;
}
