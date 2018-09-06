// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/echo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>

#include "echo_client_app.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"

int main(int argc, const char** argv) {
  std::string server_url = "echo2_server_cpp";
  std::string msg = "hello world";

  for (int i = 1; i < argc - 1; ++i) {
    if (!strcmp("--server", argv[i])) {
      server_url = argv[++i];
    } else if (!strcmp("-m", argv[i])) {
      msg = argv[++i];
    }
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  echo2::EchoClientApp app;
  app.Start(server_url);

  app.echo()->EchoString(msg, [&loop](fidl::StringPtr value) {
    printf("***** Response: %s\n", value->data());
    loop.Quit();
  });

  return loop.Run();
}
