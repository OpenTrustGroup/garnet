// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"

#include "garnet/bin/gzos/sysmgr/dynamic_service_app.h"
#include "garnet/bin/gzos/sysmgr/manifest_parser.h"

constexpr char kDefaultLabel[] = "trusty";

namespace sysmgr {

DynamicServiceApp::DynamicServiceApp()
    : startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
      root_(fbl::AdoptRef(new fs::PseudoDir)),
      vfs_(async_get_default()) {
  FXL_DCHECK(startup_context_);

  // Set up environment for the programs we will run.
  startup_context_->environment()->CreateNestedEnvironment(
      OpenAsDirectory(), env_.NewRequest(), env_controller_.NewRequest(),
      kDefaultLabel);
  env_->GetLauncher(env_launcher_.NewRequest());

  // Create default loader
  auto child = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    startup_context_->ConnectToEnvironmentService(fuchsia::sys::Loader::Name_,
                                                  std::move(channel));
    return ZX_OK;
  }));
  root_->AddEntry(fuchsia::sys::Loader::Name_, std::move(child));

  ScanPublicServices();
}

void DynamicServiceApp::ScanPublicServices() {
  ForEachManifest([this](const std::string& app_name,
                         const std::string& manifest_path,
                         const rapidjson::Document& document) {
    ParsePublicServices(document, [this, &app_name,
                                   &manifest_path](const std::string& service) {
      fbl::RefPtr<fs::Vnode> dummy;
      if (root_->Lookup(&dummy, service) == ZX_OK) {
        FXL_LOG(WARNING) << "Ignore duplicated service '" << service
                         << "' which comes from " << manifest_path;
        return;
      }

      startup_services_.emplace(service, app_name);

      RegisterService(service);
    });
  });
}

zx::channel DynamicServiceApp::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(root_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

void DynamicServiceApp::RegisterService(std::string service_name) {
  auto child = fbl::AdoptRef(new fs::Service([this, service_name](
                                                 zx::channel client_handle) {
    auto it = startup_services_.find(service_name);
    if (it != startup_services_.end()) {
      std::string app_name(it->second);

      auto it = launched_apps_.find(app_name);
      if (it == launched_apps_.end()) {
        auto app = std::make_unique<LaunchedApp>(this, app_name);
        FXL_CHECK(app);

        auto additional_services = fuchsia::sys::ServiceList::New();
        FXL_CHECK(additional_services);
        app->BindServiceProvider(additional_services->provider.NewRequest());
        additional_services->names->push_back(sysmgr::ServiceRegistry::Name_);

        fuchsia::sys::LaunchInfo launch_info;
        launch_info.url = app_name;
        launch_info.directory_request = app->service().NewRequest();
        launch_info.additional_services = std::move(additional_services);

        MountPackageData(launch_info);

        auto& controller = app->controller();
        env_launcher_->CreateComponent(std::move(launch_info),
                                       controller.NewRequest());
        controller.set_error_handler([this, app_name] {
          FXL_LOG(ERROR) << "Application " << app_name << " died";

          auto it = launched_apps_.find(app_name);
          FXL_CHECK(it != launched_apps_.end());

          auto& app = it->second;
          app->controller().Unbind();  // kills the singleton application
          app->RemoveAllServices();

          launched_apps_.erase(it);
        });

        std::tie(it, std::ignore) =
            launched_apps_.emplace(app_name, std::move(app));
      }

      it->second->service().ConnectToService(std::move(client_handle),
                                             service_name);
    }
    return ZX_OK;
  }));
  root_->AddEntry(service_name, std::move(child));
}

void DynamicServiceApp::WaitOnService(
    std::string app_name, std::string service_name,
    ServiceRegistry::WaitOnServiceCallback callback) {
  auto waiter = fbl::make_unique<ServiceWaiter>();
  FXL_DCHECK(waiter);

  waiter->app_name = app_name;
  waiter->service_name = service_name;
  waiter->callback = std::move(callback);

  fbl::AutoLock lock(&mutex_);
  waiters_.push_back(std::move(waiter));
}

void DynamicServiceApp::CancelWaitOnService(std::string app_name,
                                            std::string service_name) {
  fbl::AutoLock lock(&mutex_);
  waiters_.erase_if([app_name, service_name](const ServiceWaiter& waiter) {
    return (waiter.app_name == app_name) &&
           (waiter.service_name == service_name);
  });
}

void DynamicServiceApp::LookupService(
    std::string service_name, ServiceRegistry::LookupServiceCallback callback) {
  auto it = services_.find(service_name);
  if (it == services_.end()) {
    it = startup_services_.find(service_name);
    if (it == startup_services_.end()) {
      callback(false);
    } else {
      callback(true);
    }
  } else {
    callback(true);
  }
}

void DynamicServiceApp::AddService(std::string app_name,
                                   std::string service_name,
                                   std::function<void()> callback) {
  auto it = startup_services_.find(service_name);
  if (it != startup_services_.end()) {
    // Don't need to add service for startup service, since it is already
    // existed
    return;
  }

  it = services_.find(service_name);
  FXL_CHECK(it == services_.end());

  services_.emplace(service_name, app_name);

  auto child = fbl::AdoptRef(new fs::Service(
      [this, app_name, service_name](zx::channel client_handle) {
        auto it = launched_apps_.find(app_name);
        FXL_CHECK(it != launched_apps_.end());

        it->second->service().ConnectToService(std::move(client_handle),
                                               service_name);
        return ZX_OK;
      }));
  root_->AddEntry(service_name, std::move(child));
  callback();

  fbl::AutoLock lock(&mutex_);

  fbl::DoublyLinkedList<fbl::unique_ptr<ServiceWaiter>> tmp_list;
  while (auto waiter = waiters_.pop_front()) {
    if (waiter->service_name == service_name) {
      waiter->callback();
    } else {
      tmp_list.push_back(std::move(waiter));
    }
  }
  waiters_ = fbl::move(tmp_list);
}

void DynamicServiceApp::RemoveService(std::string service_name) {
  auto it = startup_services_.find(service_name);
  if (it != startup_services_.end()) {
    // Don't need to remove startup service
    return;
  }

  it = services_.find(service_name);
  if (it != services_.end()) {
    services_.erase(it);
  }
  root_->RemoveEntry(service_name);
}

LaunchedApp::LaunchedApp(DynamicServiceApp* app, std::string app_name)
    : app_(app), app_name_(app_name) {
  service_provider_.AddService<sysmgr::ServiceRegistry>(
      [this](fidl::InterfaceRequest<sysmgr::ServiceRegistry> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void LaunchedApp::BindServiceProvider(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  service_provider_.AddBinding(std::move(request));
}

void LaunchedApp::LookupService(
    fidl::StringPtr service_name,
    ServiceRegistry::LookupServiceCallback callback) {
  app_->LookupService(service_name, std::move(callback));
}

void LaunchedApp::AddService(fidl::StringPtr service_name) {
  app_->AddService(app_name_, service_name, [this, &service_name]() {
    registered_services_.push_back(service_name);
  });
}

void LaunchedApp::WaitOnService(fidl::StringPtr service_name,
                                WaitOnServiceCallback callback) {
  app_->WaitOnService(app_name_, service_name, std::move(callback));
}

void LaunchedApp::CancelWaitOnService(fidl::StringPtr service_name) {
  app_->CancelWaitOnService(app_name_, service_name);
}

void LaunchedApp::RemoveService(fidl::StringPtr service_name) {
  app_->RemoveService(service_name);

  auto it = registered_services_.begin();
  while (it != registered_services_.end()) {
    if (*it == service_name.get()) {
      registered_services_.erase(it);
      return;
    }
    it++;
  }
}

void LaunchedApp::RemoveAllServices() {
  for (auto& service : registered_services_) {
    app_->RemoveService(service);
  }
  registered_services_.clear();
}

}  // namespace sysmgr
