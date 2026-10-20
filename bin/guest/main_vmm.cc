// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/vmm_controller.h"
#include "garnet/lib/machina/guest_config.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"

#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"

int main(int argc, char* argv[]) {

  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  fxl::LogSettings log_settings;
  if (!fxl::ParseLogSettings(cl, &log_settings)) {
    return EXIT_FAILURE;
  }

  fxl::SetLogSettings(log_settings);

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();

  auto stop_component_callback = [&loop]() { loop.Quit(); };
  zx_status_t run_status = ZX_OK;
  {
    VmmController controller(app_context.get(), loop.async(),
                             stop_component_callback);
    zx_thread_set_priority(kLooperPriority);
    run_status = loop.Run();
  }
  app_context.reset();
  loop.Shutdown();
  return run_status;
}
