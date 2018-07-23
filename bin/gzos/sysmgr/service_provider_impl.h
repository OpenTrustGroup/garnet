// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <sysmgr/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/macros.h"

namespace sysmgr {

struct ServiceWaiter
    : public fbl::DoublyLinkedListable<fbl::unique_ptr<ServiceWaiter>> {
  std::string service_name;
  ServiceProvider::WaitOnServiceCallback callback;
};

class ServiceProviderImpl : public ServiceProvider {
 public:
  ServiceProviderImpl() : root_(fbl::AdoptRef(new fs::PseudoDir)) {}

  void set_backing_dir(zx::channel backing_dir) {
    backing_dir_ = std::move(backing_dir);
  }

  void AddBinding(fidl::InterfaceRequest<ServiceProvider> request);

 protected:
  void LookupService(fidl::StringPtr service_name,
                     LookupServiceCallback callback) override;
  void AddService(fidl::StringPtr service_name,
                  AddServiceCallback callback) override;
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;
  void WaitOnService(fidl::StringPtr service_name,
                     WaitOnServiceCallback callback) override;

 private:
  fidl::BindingSet<ServiceProvider> bindings_;
  fbl::RefPtr<fs::PseudoDir> root_;

  zx::channel backing_dir_;

  fbl::Mutex mutex_;
  fbl::DoublyLinkedList<fbl::unique_ptr<ServiceWaiter>> waiters_
      FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderImpl);
};

}  // namespace sysmgr
