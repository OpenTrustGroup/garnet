// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GATT_CONNECTION_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GATT_CONNECTION_H_

#include <memory>

#include <fbl/ref_ptr.h>
#include <lib/async/dispatcher.h>

#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"
#include "garnet/drivers/bluetooth/lib/gatt/remote_service_manager.h"

#include "lib/fxl/macros.h"

namespace btlib {

namespace l2cap {
class Channel;
}  // namespace l2cap

namespace att {
class Bearer;
class Database;
}  // namespace att

namespace gatt {

class Server;

namespace internal {

// Represents the GATT data channel between the local adapter and a single
// remote peer. A Connection supports simultaneous GATT client and server
// functionality. An instance of Connection should exist on each ACL logical
// link.
class Connection final {
 public:
  // |peer_id| is the 128-bit UUID that identifies the peer device.
  // |local_db| is the local attribute database that the GATT server will
  // operate on. |att_chan| must correspond to an open L2CAP Attribute channel.
  Connection(const std::string& peer_id,
             fxl::RefPtr<att::Bearer> att_bearer,
             fxl::RefPtr<att::Database> local_db,
             RemoteServiceWatcher svc_watcher,
             async_dispatcher_t* gatt_dispatcher);
  ~Connection() = default;

  Connection() = default;
  Connection(Connection&&) = default;
  Connection& operator=(Connection&&) = default;

  Server* server() const { return server_.get(); }
  RemoteServiceManager* remote_service_manager() const {
    return remote_service_manager_.get();
  }

  // Initiate MTU exchange followed by primary service discovery. On failure,
  // signals a link error through the ATT channel (which is expected to
  // disconnect the link).
  void Initialize();

 private:
  fxl::RefPtr<att::Bearer> att_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<RemoteServiceManager> remote_service_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace internal
}  // namespace gatt
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GATT_CONNECTION_H_
