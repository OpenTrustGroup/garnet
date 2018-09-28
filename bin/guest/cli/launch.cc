// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/launch.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/fxl/command_line.h>
#include <lib/svc/cpp/services.h>

#include "garnet/bin/guest/cli/serial.h"

void handle_launch(int argc, const char* argv[], async::Loop* loop,
                   component::StartupContext* context) {
  // Create environment.
  fuchsia::guest::EnvironmentManagerSyncPtr environment_manager;

  context->ConnectToEnvironmentService(environment_manager.NewRequest());
  fuchsia::guest::EnvironmentControllerSyncPtr environment_controller;
  environment_manager->Create(argv[0], environment_controller.NewRequest());

  // Launch guest.
  fuchsia::guest::InstanceControllerPtr instance_controller;
  fuchsia::guest::LaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.args.push_back(argv[i + 1]);
  }
  fuchsia::guest::InstanceInfo instance_info;
  environment_controller->LaunchInstance(
      std::move(launch_info), instance_controller.NewRequest(), &instance_info);
  instance_controller.set_error_handler([loop] { loop->Shutdown(); });

  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (cl.GetOptionValueWithDefault("display", "scenic") == "scenic") {
    // Create the framebuffer view.
    fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
    instance_controller->GetViewProvider(view_provider.NewRequest());
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner;
    view_provider->CreateView(view_owner.NewRequest(), nullptr);

    // Ask the presenter to display it.
    fuchsia::ui::policy::PresenterPtr presenter;
    context->ConnectToEnvironmentService(presenter.NewRequest());
    presenter->Present(std::move(view_owner), nullptr);
  }

  // Open the serial service of the guest and process IO.
  zx::socket socket;
  SerialConsole console(loop);
  instance_controller->GetSerial(
      [&console](zx::socket socket) { console.Start(std::move(socket)); });
  loop->Run();
}
