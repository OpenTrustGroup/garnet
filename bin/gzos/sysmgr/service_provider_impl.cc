// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/sysmgr/service_provider_impl.h"

#include <lib/fdio/util.h>

namespace sysmgr {

void ServiceProviderImpl::LookupService(fidl::StringPtr service_name,
                                        LookupServiceCallback callback) {
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status != ZX_OK) {
    callback(false);
  } else {
    callback(true);
  }
}

void ServiceProviderImpl::AddBinding(
    fidl::InterfaceRequest<ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderImpl::AddService(fidl::StringPtr service_name,
                                     AddServiceCallback callback) {
  root_->AddEntry(service_name->c_str(),
                  fbl::AdoptRef(new fs::Service(
                      [callback = std::move(callback)](zx::channel request) {
                        callback(std::move(request));
                        return ZX_OK;
                      })));

  fbl::AutoLock lock(&mutex_);
  if (waiters_.is_empty()) {
    return;
  }

  for (auto& waiter : waiters_) {
    if (waiter.service_name == service_name.get()) {
      waiter.callback();
      waiters_.erase(waiter);

      if (waiters_.is_empty()) {
        break;
      }
    }
  }
}

void ServiceProviderImpl::ConnectToService(fidl::StringPtr service_name,
                                           zx::channel channel) {
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status == ZX_OK) {
    child->Serve(nullptr, std::move(channel), 0);
  } else if (backing_dir_) {
    fdio_service_connect_at(backing_dir_.get(), service_name->c_str(),
                            channel.release());
  }
}

void ServiceProviderImpl::WaitOnService(fidl::StringPtr service_name,
                                        WaitOnServiceCallback callback) {
  auto waiter = fbl::make_unique<ServiceWaiter>();
  FXL_DCHECK(waiter);

  waiter->service_name = service_name.get();
  waiter->callback = std::move(callback);

  fbl::AutoLock lock(&mutex_);
  waiters_.push_back(std::move(waiter));
}

}  // namespace sysmgr
