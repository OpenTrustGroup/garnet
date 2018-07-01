// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/mdns/tool/mdns_impl.h"
#include "garnet/bin/mdns/tool/mdns_params.h"
#include "lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  mdns::MdnsParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context =
      fuchsia::sys::StartupContext::CreateFromStartupInfo();

  mdns::MdnsImpl impl(startup_context.get(), &params, [&loop]() {
    async::PostTask(loop.async(), [&loop]() { loop.Quit(); });
  });

  loop.Run();
  return 0;
}
