// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <sys/types.h>

#include "garnet/bin/sysmgr/app.h"

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/hypervisor.h>

// _trusty_ioctl deps
#include <trusty_std.h>
#include <trusty_syscalls.h>

#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace sysmgr {

constexpr char kDefaultLabel[] = "sys";
constexpr char kConfigDir[] = "/system/data/sysmgr/";

App::App()
    : application_context_(
          component::ApplicationContext::CreateFromStartupInfo()) {
  FXL_DCHECK(application_context_);

  Config config;
  char buf[PATH_MAX];
  if (strlcpy(buf, kConfigDir, PATH_MAX) >= PATH_MAX) {
    FXL_LOG(ERROR) << "Config directory path too long";
  } else {
    const size_t dir_len = strlen(buf);
    DIR* cfg_dir = opendir(kConfigDir);
    if (cfg_dir != NULL) {
      for (dirent* cfg = readdir(cfg_dir); cfg != NULL;
           cfg = readdir(cfg_dir)) {
        if (strcmp(".", cfg->d_name) == 0 || strcmp("..", cfg->d_name) == 0) {
          continue;
        }
        if (strlcat(buf, cfg->d_name, PATH_MAX) >= PATH_MAX) {
          FXL_LOG(WARNING) << "Could not read config file, path too long";
          continue;
        }
        config.ReadFrom(buf);
        buf[dir_len] = '\0';
      }
      closedir(cfg_dir);
    } else {
      FXL_LOG(WARNING) << "Could not open config directory" << kConfigDir;
    }
  }

  // Set up environment for the programs we will run.
  application_context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), kDefaultLabel);
  env_->GetApplicationLauncher(env_launcher_.NewRequest());

  // Register services.
  for (auto& pair : config.TakeServices())
    RegisterSingleton(pair.first, std::move(pair.second));

  // Ordering note: The impl of CreateNestedEnvironment will resolve the
  // delegating app loader. However, since its call back to the host directory
  // won't happen until the next (first) message loop iteration, we'll be set up
  // by then.
  RegisterAppLoaders(config.TakeAppLoaders());

  // Launch startup applications.
  for (auto& launch_info : config.TakeApps())
    LaunchApplication(std::move(*launch_info));

  // TODO(abarth): Remove this hard-coded mention of netstack once netstack is
  // fully converted to using service namespaces.
  LaunchService("provider.TaProvider");
  LaunchService("nbl_services.rpc");

  uint64_t bootmode = 0;
  zx_status_t status = zx_system_get_bootmode(&bootmode);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_bootmode fail";
  }

  if (bootmode == ZX_BOOTMODE_AHYP) {
    uint32_t uart_rx_input = 0;
    // Is magic key pressed?
    if (_trusty_ioctl(NEBULA_GENERIC_FD,
                      SYS_PLATFORM_UART_ACTION_GET_HOLD_NEBULA_SHELL_CMD,
                      (void*)&uart_rx_input) < 0) {
      FXL_LOG(ERROR) << "_trusty_ioctl failed";
    }

    if (uart_rx_input == 0) {
      // No key pressed, notify uart driver to disable RX to avoid
      // uart conflict with sos uart driver, then launch sos.
      if (_trusty_ioctl(NEBULA_GENERIC_FD,
                        SYS_PLATFORM_UART_ACTION_SET_BYPASS_NEBULA_SHELL_CMD,
                        NULL) < 0) {
        FXL_LOG(ERROR) << "_trusty_ioctl failed";
      }
      LaunchService("machina.VhmService");
    } else {
      DisableSosKickNebula();
      FXL_LOG(ERROR) << "Nebula shell start, won't launch sos here";
    }
  } else if (bootmode == ZX_BOOTMODE_TEE) {
    LaunchService("nebula.GpManager");
  }

}

App::~App() {}

void App::RegisterSingleton(std::string service_name,
                            component::ApplicationLaunchInfoPtr launch_info) {
  service_provider_bridge_.AddServiceForName(
      fxl::MakeCopyable([this, service_name,
                         launch_info = std::move(launch_info),
                         controller = component::ApplicationControllerPtr()](
                            zx::channel client_handle) mutable {
        FXL_VLOG(2) << "Servicing singleton service request for "
                    << service_name;
        auto it = services_.find(launch_info->url);
        if (it == services_.end()) {
          FXL_VLOG(1) << "Starting singleton " << launch_info->url
                      << " for service " << service_name;
          component::Services services;
          component::ApplicationLaunchInfo dup_launch_info;
          dup_launch_info.url = launch_info->url;
          fidl::Clone(launch_info->arguments, &dup_launch_info.arguments);
          dup_launch_info.directory_request = services.NewRequest();
          env_launcher_->CreateApplication(std::move(dup_launch_info),
                                           controller.NewRequest());
          controller.set_error_handler(
              [ this, url = launch_info->url, &controller ] {
                FXL_LOG(ERROR) << "Singleton " << url << " died";
                controller.Unbind();  // kills the singleton application
                services_.erase(url);
#ifdef __Nebula__
                if (url == "/system/bin/nbl_loader_srv") {
                  zx_nanosleep(zx_deadline_after(ZX_SEC(2)));
                  LaunchService("provider.TaProvider");
                }
#endif
              });

          std::tie(it, std::ignore) =
              services_.emplace(launch_info->url, std::move(services));
        }

        it->second.ConnectToService(std::move(client_handle), service_name);
      }),
      service_name);
}

void App::RegisterAppLoaders(Config::ServiceMap app_loaders) {
  app_loader_ = std::make_unique<DelegatingApplicationLoader>(
      std::move(app_loaders), env_launcher_.get(),
      application_context_
          ->ConnectToEnvironmentService<component::ApplicationLoader>());

  service_provider_bridge_.AddService<component::ApplicationLoader>(
      [this](fidl::InterfaceRequest<component::ApplicationLoader> request) {
        app_loader_bindings_.AddBinding(app_loader_.get(), std::move(request));
      });
}

void App::LaunchApplication(component::ApplicationLaunchInfo launch_info) {
  FXL_VLOG(1) << "Launching application " << launch_info.url;
  env_launcher_->CreateApplication(std::move(launch_info), nullptr);
}

void App::LaunchService(const std::string& service_name) {
  FXL_LOG(INFO) << "Launching service " << service_name;
  auto provider = static_cast<component::ServiceProvider*>(&service_provider_bridge_);
  zx::channel h1, h2;
  zx::channel::create(0, &h1, &h2);
  provider->ConnectToService(service_name, std::move(h1));
}

void App::DisableSosKickNebula() {
  int fd = 0;

  if (access(GRT_WDT_CONTROL_DEVICE, F_OK) != 0) {
    printf("wdt driver is not ready:%s\n",GRT_WDT_CONTROL_DEVICE);
    return;
  }
  fd = open(GRT_WDT_CONTROL_DEVICE, O_RDWR);
  if (fd < 0) {
    printf("driver-tests: no %s device found\n", GRT_WDT_CONTROL_DEVICE);
    return;
  }

  ioctl_grt_wdt_stop(fd);
  close(fd);
  return;
}

}  // namespace sysmgr
