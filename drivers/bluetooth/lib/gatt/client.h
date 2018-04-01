// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/att/bearer.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace gatt {

// Implements GATT client-role procedures. A client operates over a single ATT
// data bearer. Client objects are solely used to map GATT procedures to ATT
// protocol methods and do not maintain service state.
//
// THREAD SAFETY:
//
// Client is not thread safe. It must be created, used, and destroyed on the
// same thread. All asynchronous callbacks are run on the thread that the data
// bearer is bound to.
class Client final {
 public:
  explicit Client(fxl::RefPtr<att::Bearer> bearer);
  ~Client() = default;

  // Initiates an MTU exchange and adjusts the MTU of the bearer according to
  // what the peer is capable of. The request will be initiated using the
  // bearer's preferred MTU.
  //
  // After the exchange is complete, the bearer will be updated to use the
  // resulting MTU. The resulting MTU will be notified via |callback|.
  //
  // |status| will be set to an error if the MTU exchange fails. The |mtu|
  // parameter will be set to 0 and the underlying bearer's MTU will remain
  // unmodified.
  //
  // TODO(NET-288): Return a att::Status here instead.
  using MTUCallback = std::function<void(att::ErrorCode status, uint16_t mtu)>;
  void ExchangeMTU(MTUCallback callback);

 private:
  // Wraps |callback| in a TransactionCallback that only runs if this Client is
  // still alive.
  att::Bearer::TransactionCallback BindCallback(
      att::Bearer::TransactionCallback callback);

  // Wraps |callback| in a ErrorCallback that only runs if this Client is still
  // alive.
  att::Bearer::ErrorCallback BindErrorCallback(
      att::Bearer::ErrorCallback callback);

  fxl::RefPtr<att::Bearer> att_;

  fxl::WeakPtrFactory<Client> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Client);
};

}  // namespace gatt
}  // namespace btlib
