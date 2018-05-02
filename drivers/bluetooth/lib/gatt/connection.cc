// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connection.h"

#include "garnet/drivers/bluetooth/lib/att/database.h"

#include "client.h"
#include "server.h"

namespace btlib {
namespace gatt {
namespace internal {

Connection::Connection(const std::string& peer_id,
                       fxl::RefPtr<att::Bearer> att_bearer,
                       fxl::RefPtr<att::Database> local_db,
                       RemoteServiceWatcher svc_watcher,
                       async_t* gatt_dispatcher) {
  FXL_DCHECK(att_bearer);
  FXL_DCHECK(local_db);
  FXL_DCHECK(svc_watcher);
  FXL_DCHECK(gatt_dispatcher);

  server_ = std::make_unique<gatt::Server>(peer_id, local_db, att_bearer);
  remote_service_manager_ = std::make_unique<RemoteServiceManager>(
      gatt::Client::Create(att_bearer), gatt_dispatcher);

  remote_service_manager_->set_service_watcher(std::move(svc_watcher));
  remote_service_manager_->Initialize([att_bearer](att::Status status) {
    if (status) {
      FXL_VLOG(1) << "gatt: Primary service discovery complete";
    } else {
      FXL_VLOG(1) << "gatt: Client setup failed - " << status.ToString();

      // Signal a link error.
      att_bearer->ShutDown();
    }
  });
}

Connection::~Connection() {}

}  // namespace internal
}  // namespace gatt
}  // namespace btlib
