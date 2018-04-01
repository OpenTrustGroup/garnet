// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <trace-provider/provider.h>

#include "garnet/bin/media/media_service/media_component_factory.h"
#include "lib/fsl/tasks/message_loop.h"
#include <fuchsia/cpp/media.h>
#include "lib/svc/cpp/services.h"

const std::string kIsolateUrl = "media_service";
const std::string kIsolateArgument = "--transient";

// Connects to the requested service in a media_service isolate.
template <typename Interface>
void ConnectToIsolate(fidl::InterfaceRequest<Interface> request,
                      component::ApplicationLauncher* launcher) {
  component::ApplicationLaunchInfo launch_info;
  launch_info.url = kIsolateUrl;
  launch_info.arguments.push_back(kIsolateArgument);
  component::Services services;
  launch_info.directory_request = services.NewRequest();

  component::ApplicationControllerPtr controller;
  launcher->CreateApplication(std::move(launch_info), controller.NewRequest());

  services.ConnectToService(std::move(request), Interface::Name_);

  controller->Detach();
}

int main(int argc, const char** argv) {
  bool transient = false;
  for (int arg_index = 0; arg_index < argc; ++arg_index) {
    if (argv[arg_index] == kIsolateArgument) {
      transient = true;
      break;
    }
  }

  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  if (transient) {
    media::MediaComponentFactory factory(std::move(application_context));

    factory.application_context()
        ->outgoing_services()
        ->AddService<media::MediaPlayer>(
            [&factory](fidl::InterfaceRequest<media::MediaPlayer> request) {
              factory.CreateMediaPlayer(std::move(request));
            });

    loop.Run();
  } else {
    component::ApplicationLauncherPtr launcher;
    application_context->environment()->GetApplicationLauncher(
        launcher.NewRequest());

    application_context->outgoing_services()->AddService<media::MediaPlayer>(
        [&launcher](fidl::InterfaceRequest<media::MediaPlayer> request) {
          ConnectToIsolate<media::MediaPlayer>(std::move(request),
                                               launcher.get());
        });

    loop.Run();
  }

  return 0;
}
