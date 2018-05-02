// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_set>
#include <fbl/function.h>

#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"
#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"

namespace btlib {
namespace gatt {

// Callback called to signal that an indication payload should be sent. Used to
// inject the GATT object's notification sending functionality (avoiding this
// service from carrying a reference to GATT or Server).
using SendIndicationCallback = fbl::Function<void(
    const std::string& peer_id,
    att::Handle handle,
    const common::ByteBuffer& value)>;

// Implements the "Generic Attribute Profile Service" containing the "Service
// Changed" characteristic that is "...used to indicate to connected devices
// that services have changed (Vol 3, Part G, 7)."
class GenericAttributeService final {
 public:
  // Registers this service and makes this service the callee of the Service
  // Changed callback. GATT remote clients must still request that they be sent
  // indications for the Service Changed characteristic. Holds the
  // LocalServiceManager pointer for the duration of this object. Do not
  // register this with multiple LocalServiceManagers.
  GenericAttributeService(LocalServiceManager* local_service_manager,
                          SendIndicationCallback send_indication_callback);
  ~GenericAttributeService();


 private:
  void Register();

  // Send indications to subscribed clients when a service has changed.
  void OnServiceChanged(IdType service_id, att::Handle start, att::Handle end);

  // Data store against which to register and unregister this service. It must
  // outlive this instance.
  LocalServiceManager* const local_service_manager_;
  const SendIndicationCallback send_indication_callback_;

  // Peers that have subscribed to indications.
  std::unordered_set<std::string> subscribed_peers_;

  // Handle for the Service Changed characteristic that is read when it is first
  // configured for indications.
  att::Handle svc_changed_handle_ = att::kInvalidHandle;

  // Local service ID; hidden because registration is tied to instance lifetime.
  IdType service_id_ = kInvalidId;
};

}  // namespace gatt
}  // namespace btlib
