// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/drivers/bluetooth/lib/att/att.h"
#include "garnet/drivers/bluetooth/lib/att/bearer.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"

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
class Client {
 public:
  // Constructs a new Client bearer.
  static std::unique_ptr<Client> Create(fxl::RefPtr<att::Bearer> bearer);

  virtual ~Client() = default;

  // Returns a weak pointer to this Client. The weak pointer should be checked
  // on the data bearer's thread only as Client can only be accessed on that
  // thread.
  virtual fxl::WeakPtr<Client> AsWeakPtr() = 0;

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
  using MTUCallback = std::function<void(att::Status status, uint16_t mtu)>;
  virtual void ExchangeMTU(MTUCallback callback) = 0;

  // Performs the "Discover All Primary Services" procedure defined in
  // v5.0, Vol 3, Part G, 4.4.1. |service_callback| is run for each discovered
  // service. |status_callback| is run with the result of the operation.
  //
  // NOTE: |service_callback| will be called asynchronously as services are
  // discovered so a caller can start processing the results immediately while
  // the procedure is in progress. Since discovery usually occurs over multiple
  // ATT transactions, it is possible for |status_callback| to be called with an
  // error even if some services have been discovered. It is up to the client
  // to clear any cached state in this case.
  using ServiceCallback = std::function<void(const ServiceData&)>;
  virtual void DiscoverPrimaryServices(ServiceCallback svc_callback,
                                       att::StatusCallback status_callback) = 0;

  // Performs the "Discover All Characteristics of a Service" procedure defined
  // in v5.0, Vol 3, Part G, 4.6.1.
  using CharacteristicCallback = std::function<void(const CharacteristicData&)>;
  virtual void DiscoverCharacteristics(att::Handle range_start,
                                       att::Handle range_end,
                                       CharacteristicCallback chrc_callback,
                                       att::StatusCallback status_callback) = 0;

  // Performs the "Discover All Characteristic Descriptors" procedure defined in
  // Vol 3, Part G, 4.7.1.
  using DescriptorCallback = std::function<void(const DescriptorData&)>;
  virtual void DiscoverDescriptors(att::Handle range_start,
                                   att::Handle range_end,
                                   DescriptorCallback desc_callback,
                                   att::StatusCallback status_callback) = 0;

  // Sends an ATT Write Request with the requested attribute |handle| and
  // |value|. This can be used to send a write request to any attribute.
  // (Vol 3, Part F, 3.4.5.1).
  //
  // Reports the status of the procedure in |callback|.
  // HostError::kPacketMalformed is returned if |value| is too large to write in
  // a single ATT request.
  virtual void WriteRequest(att::Handle handle,
                            const common::ByteBuffer& value,
                            att::StatusCallback callback) = 0;
};

}  // namespace gatt
}  // namespace btlib
