// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <sysmgr/cpp/fidl.h>

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

// The dynamic service model is created just for trusty application only, since
// without dirty hacks, it is not easy to implement trusty api using fuchsia's
// standard service model. Thus we create additional fidl interface for
// trusty app only. This may be dropped in the future.
//
// DO NOT create additional customized service model for other protocols. We
// should follow fuchsia's standard.

namespace sysmgr {

class DynamicServiceApp;
class LaunchedApp : public ServiceRegistry {
 public:
  LaunchedApp(DynamicServiceApp* app, std::string app_name);
  LaunchedApp() = delete;

  void BindServiceProvider(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>);
  void RemoveService(std::string service_name);
  void RemoveAllServices();

  auto& controller() { return controller_; }
  auto& service() { return service_; }

 protected:
  // Overridden from |sysmgr::ServiceRegistry|
  void LookupService(fidl::StringPtr service_name,
                     LookupServiceCallback callback) override;
  void AddService(fidl::StringPtr service_name) override;
  void RemoveService(fidl::StringPtr service_name) override;
  void WaitOnService(fidl::StringPtr service_name,
                     WaitOnServiceCallback callback) override;
  void CancelWaitOnService(fidl::StringPtr service_name) override;

 private:
  fidl::BindingSet<ServiceRegistry> bindings_;

  DynamicServiceApp* app_;
  std::string app_name_;

  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::sys::Services service_;
  fuchsia::sys::ServiceProviderImpl service_provider_;

  std::vector<std::string> registered_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LaunchedApp);
};

class DynamicServiceApp {
 public:
  explicit DynamicServiceApp();
  ~DynamicServiceApp() = default;

  void AddService(std::string app_name, std::string service_name,
                  std::function<void()> callback);
  void RemoveService(std::string service_name);
  void LookupService(std::string service_name,
                     ServiceRegistry::LookupServiceCallback callback);
  void WaitOnService(std::string app_name, std::string service_name,
                     ServiceRegistry::WaitOnServiceCallback callback);
  void CancelWaitOnService(std::string app_name, std::string service_name);

 private:
  zx::channel OpenAsDirectory();
  void ScanPublicServices();
  void RegisterService(std::string service_name);

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  // map of service name and app name
  std::map<std::string, std::string> services_;
  std::map<std::string, std::string> startup_services_;

  // map of app name and it's service provider
  std::map<std::string, std::unique_ptr<LaunchedApp>> launched_apps_;

  // Nested environment within which the apps started by sysmgr will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr env_launcher_;

  fbl::RefPtr<fs::PseudoDir> root_;
  fs::SynchronousVfs vfs_;

  struct ServiceWaiter
      : public fbl::DoublyLinkedListable<fbl::unique_ptr<ServiceWaiter>> {
    std::string app_name;
    std::string service_name;
    ServiceRegistry::WaitOnServiceCallback callback;
  };

  fbl::Mutex mutex_;
  fbl::DoublyLinkedList<fbl::unique_ptr<ServiceWaiter>> waiters_
      FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(DynamicServiceApp);
};

}  // namespace sysmgr
