// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <lib/async-loop/cpp/loop.h>
#include <presentation/cpp/fidl.h>
#include <trace-provider/provider.h>

#include "garnet/examples/ui/video_display/view.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/ui/view_framework/view_provider_app.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  bool use_fake_camera = command_line.HasOption("fake_camera");

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.async());

  mozart::ViewProviderApp app([&loop, use_fake_camera](
                                  mozart::ViewContext view_context) {
    return std::make_unique<video_display::View>(
        &loop,
        view_context.application_context, std::move(view_context.view_manager),
        std::move(view_context.view_owner_request), use_fake_camera);
  });

  loop.Run();
  return 0;
}
