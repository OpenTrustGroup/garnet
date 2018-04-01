// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"
#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"
#include "garnet/drivers/bluetooth/lib/gatt/types.h"

#include "lib/fidl/cpp/vector.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {

namespace l2cap {
class Channel;
}  // namespace l2cap

namespace gatt {

// This is the root object of the GATT layer. This object owns:
//
//   * A single local attribute database
//   * All client and server data bearers
//   * L2CAP ATT fixed channels
//
// GATT requires a TaskRunner on initialization which will be used to serially
// dispatch all internal GATT tasks.
//
// All public functions are asynchronous and thread-safe.
class GATT : public fbl::RefCounted<GATT> {
 public:
  // Constructs a GATT object.
  static fbl::RefPtr<GATT> Create(fxl::RefPtr<fxl::TaskRunner> gatt_runner);

  // Initialize/ShutDown the GATT profile. It is safe for the caller to drop its
  // reference after ShutDown.
  //
  // The owner MUST call ShutDown() to properly clean up the object.
  virtual void Initialize() = 0;
  virtual void ShutDown() = 0;

  // Initializes a new GATT profile connection with the given peer. The GATT
  // profile is implemented over the L2CAP ATT fixed channel.
  //
  // |peer_id|: The identifier for the peer device that the link belongs to.
  //            This is used to identify the peer while handling certain events.
  // |att_chan|: The ATT fixed channel over which the ATT protocol bearer will
  //             operate. The bearer will be associated with the link that
  //             underlies this channel.
  virtual void AddConnection(const std::string& peer_id,
                             fbl::RefPtr<l2cap::Channel> att_chan) = 0;

  // Unregisters the GATT profile connection to the peer with Id |peer_id|.
  virtual void RemoveConnection(std::string peer_id) = 0;

  // ==============
  // Local Services
  // ==============
  //
  // The methods below are for managing local GATT services that are available
  // to data bearers in the server role.

  // Registers the GATT service hierarchy represented by |service| with the
  // local attribute database. Once successfully registered, the service will
  // be available to remote clients.
  //
  // Objects under |service| must have unique identifiers to aid in value
  // request handling. These identifiers will be passed to |read_handler| and
  // |write_handler|.
  //
  // The provided handlers will be called to handle remote initiated
  // transactions targeting the service. These handlers will be run on the
  // on the GATT task runner.
  //
  // This method returns an opaque identifier on successful registration,
  // which can be used by the caller to refer to the service in the future.
  //
  // Returns |kInvalidId| on failure. Registration can fail if the attribute
  // database has run out of handles or if the hierarchy contains
  // characteristics or descriptors with repeated IDs.
  using ServiceIdCallback = std::function<void(IdType)>;
  virtual void RegisterService(ServicePtr service,
                               ServiceIdCallback callback,
                               ReadHandler read_handler,
                               WriteHandler write_handler,
                               ClientConfigCallback ccc_callback,
                               fxl::RefPtr<fxl::TaskRunner> task_runner) = 0;

  // Unregisters the GATT service hierarchy identified by |service_id|. Has no
  // effect if |service_id| is not a registered id.
  virtual void UnregisterService(IdType service_id) = 0;

  // Sends a characteristic handle-value notification to a peer that has
  // configured the characteristic for notifications or indications. Does
  // nothing if the given peer has not configured the characteristic.
  //
  // |service_id|: The GATT service that the characteristic belongs to.
  // |chrc_id|: The GATT characteristic that will be notified.
  // |peer_id|: ID of the peer that the notification/indication will be sent to.
  // |value|: The attribute value that will be included in the notification.
  // |indicate|: If true, an indication will be sent.
  //
  // This method can only be called on the GATT task runner.
  //
  // TODO(armansito): Revise this API to involve fewer lookups (NET-483).
  // TODO(armansito): Fix this to notify all registered peers when |peer_id| is
  // empty (NET-589).
  virtual void SendNotification(IdType service_id,
                                IdType chrc_id,
                                std::string peer_id,
                                ::fidl::VectorPtr<uint8_t> value,
                                bool indicate) = 0;

 protected:
  friend class fbl::RefPtr<GATT>;
  GATT() = default;
  virtual ~GATT() = default;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(GATT);
};

}  // namespace gatt
}  // namespace btlib
