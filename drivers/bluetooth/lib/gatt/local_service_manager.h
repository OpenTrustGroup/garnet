// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/att/attribute.h"
#include "garnet/drivers/bluetooth/lib/att/database.h"
#include "garnet/drivers/bluetooth/lib/gatt/types.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {
namespace gatt {

// Called to read the value of a dynamic characteristic or characteristic
// descriptor.
//   - |service_id|: Identifies the service that the object belongs to.
//   - |id|: Identifies the object to be read. This is a user assigned
//           identifier provided while registering the service.
//   - |offset|: The offset into the value that is being read.
//   - |responder|: Should be called to respond to the read request with a
//                  characteristic or descriptor value, or an ATT error code.
using ReadResponder = att::Attribute::ReadResultCallback;
using ReadHandler = std::function<void(IdType service_id,
                                       IdType id,
                                       uint16_t offset,
                                       const ReadResponder& responder)>;

// Called to write the value of a dynamic characteristic or characteristic
// descriptor.
//   - |service_id|: Identifies the service that the object belongs to.
//   - |id|: Identifies the object to be written. This is a user assigned
//           identifier provided while registering the service.
//   - |offset|: The offset into the value that is being written.
//   - |responder|: Should be called to respond to the write request with
//                  success or an ATT error code. This can be a null callback
//                  if the client has initiated a "Write Without Response"
//                  procedure, in which case a response is not required.
using WriteResponder = att::Attribute::WriteResultCallback;
using WriteHandler = std::function<void(IdType service_id,
                                        IdType id,
                                        uint16_t offset,
                                        const common::ByteBuffer& value,
                                        const WriteResponder& responder)>;

// Called when the peer device with the given |peer_id| has enabled or disabled
// notifications/indications on the characteristic with id |chrc_id|.
using ClientConfigCallback = std::function<void(IdType service_id,
                                                IdType chrc_id,
                                                const std::string& peer_id,
                                                bool notify,
                                                bool indicate)>;

// LocalServiceManager allows clients to implement GATT services. This
// internally maintains an attribute database and provides hooks for clients to
// respond to read and write requests, send notifications/indications,
// add/remove services, etc.
class LocalServiceManager final {
 public:
  LocalServiceManager();
  ~LocalServiceManager();

  // Registers the GATT service hierarchy represented by |service| with the
  // local attribute database. Once successfully registered, the service will be
  // available for discovery and other ATT protocol requests.
  //
  // This method returns an opaque identifier on successful registration, which
  // can be used by the caller to refer to the service in the future.
  //
  // Returns |kInvalidId| on failure. Registration can fail if the attribute
  // database has run out of handles or if the hierarchy contains
  // characteristics or descriptors with repeated IDs. Objects under |service|
  // must have unique identifiers to aid in value request handling.
  IdType RegisterService(ServicePtr service,
                         ReadHandler read_handler,
                         WriteHandler write_handler,
                         ClientConfigCallback ccc_callback);

  // Unregisters the GATT service hierarchy identified by |service_id|. Returns
  // false if |service_id| is unrecognized.
  bool UnregisterService(IdType service_id);

  // Returns the client characteristic configuration for the given |peer_id| and
  // the characteristic identified by |service_id| and |chrc_id|. Returns false
  // if |service_id| is unknown or no configurations exist for |chrc_id|.
  struct ClientCharacteristicConfig {
    att::Handle handle;
    bool notify;
    bool indicate;
  };
  bool GetCharacteristicConfig(IdType service_id,
                               IdType chrc_id,
                               const std::string& peer_id,
                               ClientCharacteristicConfig* out_config);

  // Erase any client characteristic configuration associated to a specific
  // client and invoke its ClientConfigCallback to signal that notifications and
  // indications are now disabled.
  void DisconnectClient(const std::string& peer_id);

  fxl::RefPtr<att::Database> database() const { return db_; }

 private:
  class ServiceData;

  fxl::RefPtr<att::Database> db_;
  IdType next_service_id_;

  // Mapping from service instance ids to ServiceData.
  std::unordered_map<IdType, std::unique_ptr<ServiceData>> services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LocalServiceManager);
};

}  // namespace gatt
}  // namespace btlib
