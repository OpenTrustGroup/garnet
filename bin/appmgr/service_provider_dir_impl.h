// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
#define GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace component {

// A directory-like object which dynamically creates Service vnodes
// for any file lookup. It also exposes service provider interface.
//
// It supports enumeration for only first level of services.
class ServiceProviderDirImpl : public fuchsia::sys::ServiceProvider,
                               public fs::Vnode {
 public:
  explicit ServiceProviderDirImpl(
      const std::vector<std::string>* services = nullptr);
  ~ServiceProviderDirImpl() override;

  // Sets the parent of this. Parent should be fully initialized.
  void set_parent(fbl::RefPtr<ServiceProviderDirImpl> parent);

  void set_component_url(const std::string& url) {
    component_url_ = url;
  }

  void AddService(const std::string& service_name,
                  fbl::RefPtr<fs::Service> service);

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request);

  //
  // Overridden from |fs::Vnode|:
  //

  zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;

  zx_status_t Getattr(vnattr_t* a) final;

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                      size_t* out_actual) final;

  //
  // Overridden from |fuchsia::sys::ServiceProvider|:
  //

  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;

 private:
  bool IsServiceWhitelisted(const std::string& service_name) {
    return (!has_services_whitelist_ ||
            services_whitelist_.count(service_name) > 0);
  }

  fidl::BindingSet<fuchsia::sys::ServiceProvider> bindings_;
  fs::SynchronousVfs vfs_;
  // |root_| has all services offered by this provider (including those
  // inherited from the parent, if any).
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<ServiceProviderDirImpl> parent_;
  fxl::WeakPtrFactory<ServiceProviderDirImpl> weak_factory_;

  // TODO(CP-25): Remove has_services_whitelist_ when empty services is
  // equivalent to no services.
  bool has_services_whitelist_ = false;
  std::unordered_set<std::string> services_whitelist_;
  // Secondary storage for services under |root_| in a format that can easily
  // be consumed by children. Stored as vector to preserve order.
  typedef std::pair<std::string, fbl::RefPtr<fs::Service>> ServiceHandle;
  std::vector<ServiceHandle> service_handles_;
  std::unordered_set<std::string> all_service_names_;

  std::string component_url_ = "NO_COMPONENT";

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderDirImpl);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
