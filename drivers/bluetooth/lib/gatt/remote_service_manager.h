// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <fbl/ref_ptr.h>
#include <lib/async/dispatcher.h>

#include "garnet/drivers/bluetooth/lib/att/status.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include "garnet/drivers/bluetooth/lib/gatt/remote_service.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace gatt {

class Client;

namespace internal {

// Maintains a collection of services that are present on a particular peer
// device. The RemoteServiceManager owns the GATT client data bearer, interacts
// with the profile-defined GATT service, and allows observers to be notified
// as services get discovered.
//
// THREAD SAFETY:
//
// This class is NOT thread safe. It must be created, used, and destroyed on a
// specific thread.
class RemoteServiceManager final {
 public:
  RemoteServiceManager(std::unique_ptr<Client> client,
                       async_t* gatt_dispatcher);
  ~RemoteServiceManager();

  // Adds a handler to be notified when a new service is added.
  void set_service_watcher(RemoteServiceWatcher watcher) {
    FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
    svc_watcher_ = std::move(watcher);
  }

  // Initiates the Exchange MTU procedure followed by primary service
  // discovery. |callback| is called to notify the result of the procedure.
  void Initialize(att::StatusCallback callback);

  // Returns a vector containing discovered services that match any of the given
  // |uuids| via |callback|. All services will be returned if |uuids| is empty.
  //
  // If called while uninitialized, |callback| will be run after initialization.
  void ListServices(const std::vector<common::UUID>& uuids,
                    ServiceListCallback callback);

  // Returns the RemoteService with the requested range start |handle| or
  // nullptr if it is not recognized. This method may fail if called before or
  // during initialization.
  fbl::RefPtr<RemoteService> FindService(att::Handle handle);

 private:
  using ServiceMap = std::map<att::Handle, fbl::RefPtr<RemoteService>>;

  // Used to represent a queued ListServices() request.
  class ServiceListRequest {
   public:
    ServiceListRequest(ServiceListCallback callback,
                       const std::vector<common::UUID>& uuids);

    ServiceListRequest() = default;
    ServiceListRequest(ServiceListRequest&&) = default;

    // Completes this request by using entries from |services| that match any of
    // the requested UUIDs.
    void Complete(att::Status status, const ServiceMap& services);

   private:
    ServiceListCallback callback_;
    std::vector<common::UUID> uuids_;

    FXL_DISALLOW_COPY_AND_ASSIGN(ServiceListRequest);
  };

  // Shuts down and cleans up all services.
  void ClearServices();

  async_t* gatt_dispatcher_;
  std::unique_ptr<Client> client_;

  bool initialized_;
  RemoteServiceWatcher svc_watcher_;

  // Requests queued during calls ListServices() before initialization.
  std::queue<ServiceListRequest> pending_;

  // Services are sorted by handle.
  ServiceMap services_;

  fxl::ThreadChecker thread_checker_;
  fxl::WeakPtrFactory<RemoteServiceManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteServiceManager);
};

}  // namespace internal
}  // namespace gatt
}  // namespace btlib
