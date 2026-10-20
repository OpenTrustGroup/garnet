// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "garnet/bin/sysmgr/app.h"
#include "garnet/lib/machina/watchdog_policy.h"

#include <zircon/process.h>
#include <zircon/device/grt-wdt.h>
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
constexpr char kGuestManagerService[] = "fuchsia.virtualization.GuestManager";

namespace {
  std::vector<std::string> auto_restart_services;
  bool AutoRestartService(const std::string& service_name) {
    for (const auto& name : auto_restart_services) {
      if (name == service_name) {
        return true;
      }
    }
    return false;
  }
}

App::App()
    : application_context_(
          component::ApplicationContext::CreateFromStartupInfo()) {
  FXL_DCHECK(application_context_);
  LoadFeatures();

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
  LaunchService("provider.TaProvider", true);
  LaunchService("nbl_services.rpc");

  // LVGL demo
  LaunchService("SpiService.Transport");

  uint64_t bootmode = 0;
  zx_status_t status = zx_system_get_bootmode(&bootmode);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_bootmode fail";
  }

  if (bootmode == ZX_BOOTMODE_AHYP) {
    if (features_.nebulalog_enabled()) {
      LaunchService("nebula.LogService", true);
    }
    const machina::WatchdogPolicy watchdog_policy(features_.wdt_sink_enabled());
    FXL_LOG(INFO) << "guest_manager watchdog policy=" << watchdog_policy.name()
                  << " auto_restart=" << watchdog_policy.IsSink();
    LaunchService(kGuestManagerService, watchdog_policy.IsSink());
  } else if (bootmode == ZX_BOOTMODE_TEE) {
    LaunchService("nebula.GpManager");
  }

}

App::~App() {}

void App::LoadFeatures() {
  features::FeatureSet loaded_features;
  zx_status_t status = features::FeatureSet::LoadFromSysinfo(&loaded_features);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load platform features: " << status;
    return;
  }
  features_ = loaded_features;
}

void App::RegisterSingleton(std::string service_name,
                            component::ApplicationLaunchInfoPtr launch_info) {
  if (service_name == kGuestManagerService) {
    guest_manager_url_ = launch_info->url;
  }
  service_provider_bridge_.AddServiceForName(
      fxl::MakeCopyable([this, service_name,
                         launch_info = std::move(launch_info),
                         controller = component::ApplicationControllerPtr()](
                            zx::channel client_handle) mutable {
        FXL_VLOG(2) << "Servicing singleton service request for "
                    << service_name;
        // Compare the component URL so RprocService/CrossVMLock aliases cannot
        // launch a fresh GuestManager while the platform is awaiting reset.
        if (guest_manager_reset_requested_ &&
            launch_info->url == guest_manager_url_) {
          FXL_LOG(ERROR) << "GuestManager connection blocked pending platform "
                           "reset: " << service_name;
          return;
        }
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
              [ this, name = service_name, url = launch_info->url, &controller ] {
                FXL_LOG(ERROR) << "Singleton " << url << " died";
                HandleSingletonFailure(url);
                controller.Unbind();  // kills the singleton application
                services_.erase(url);
#ifdef __Nebula__
                if (AutoRestartService(name)) {
                  zx_nanosleep(zx_deadline_after(ZX_SEC(2)));
                  LaunchService(name);
                }
#endif
              });
#ifdef __Nebula__
          controller->WaitLaunchResponse(
              [this, name = service_name, url = launch_info->url,
               &controller](int32_t launch_res) {
                if (launch_res == ZX_OK) {
                  return;
                }
                FXL_LOG(ERROR) << "Singleton " << url
                               << " failed to launch: " << launch_res;
                HandleSingletonFailure(url);
                controller.Unbind();
                services_.erase(url);
                if (AutoRestartService(name)) {
                  zx_nanosleep(zx_deadline_after(ZX_SEC(2)));
                  LaunchService(name);
                }
              });
#endif

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

void App::LaunchService(const std::string& service_name, bool auto_restart) {
  if (guest_manager_reset_requested_ && service_name == kGuestManagerService) {
    FXL_LOG(ERROR) << "GuestManager launch blocked pending platform reset";
    return;
  }
  FXL_LOG(INFO) << "Launching service " << service_name << ", auto_restart: "
                << (auto_restart ? "true" : "false");
  auto provider = static_cast<component::ServiceProvider*>(&service_provider_bridge_);
  zx::channel h1, h2;
  zx::channel::create(0, &h1, &h2);
  provider->ConnectToService(service_name, std::move(h1));
  if (auto_restart) {
    auto_restart_services.push_back(service_name);
  }
}

void App::HandleSingletonFailure(const std::string& url) {
  const machina::WatchdogPolicy watchdog_policy(features_.wdt_sink_enabled());
  if (watchdog_policy.IsSink() || guest_manager_url_.empty() ||
      url != guest_manager_url_ || guest_manager_reset_requested_) {
    return;
  }

  // Latch before controller cleanup or service-map erasure makes another lazy
  // launch possible. A failed reset ioctl must not reopen that launch path.
  guest_manager_reset_requested_ = true;
  FXL_LOG(ERROR) << "GuestManager failed, policy=" << watchdog_policy.name()
                 << "; requesting platform reset";
  const int fd = open(GRT_WDT_CONTROL_DEVICE, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open watchdog control for platform reset";
    return;
  }
  const ssize_t result = ioctl_grt_wdt_trigger_reset(fd);
  close(fd);
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to request platform reset: " << result;
  }
}

}  // namespace sysmgr
