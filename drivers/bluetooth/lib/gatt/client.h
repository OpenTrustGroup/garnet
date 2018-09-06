// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GATT_CLIENT_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GATT_CLIENT_H_

#include <lib/fit/function.h>

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

  // Returns the current ATT MTU.
  virtual uint16_t mtu() const = 0;

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
  using MTUCallback = fit::function<void(att::Status status, uint16_t mtu)>;
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
  using ServiceCallback = fit::function<void(const ServiceData&)>;
  virtual void DiscoverPrimaryServices(ServiceCallback svc_callback,
                                       att::StatusCallback status_callback) = 0;

  // Performs the "Discover All Characteristics of a Service" procedure defined
  // in v5.0, Vol 3, Part G, 4.6.1.
  using CharacteristicCallback = fit::function<void(const CharacteristicData&)>;
  virtual void DiscoverCharacteristics(att::Handle range_start,
                                       att::Handle range_end,
                                       CharacteristicCallback chrc_callback,
                                       att::StatusCallback status_callback) = 0;

  // Performs the "Discover All Characteristic Descriptors" procedure defined in
  // Vol 3, Part G, 4.7.1.
  using DescriptorCallback = fit::function<void(const DescriptorData&)>;
  virtual void DiscoverDescriptors(att::Handle range_start,
                                   att::Handle range_end,
                                   DescriptorCallback desc_callback,
                                   att::StatusCallback status_callback) = 0;

  // Sends an ATT Read Request with the requested attribute |handle| and returns
  // the resulting value in |callback|. This can be used to send a (short) read
  // request to any attribute. (Vol 3, Part F, 3.4.4.3).
  //
  // Reports the status of the procedure and the resulting value in |callback|.
  // Returns an empty buffer if the status is an error.
  using ReadCallback =
      fit::function<void(att::Status, const common::ByteBuffer&)>;
  virtual void ReadRequest(att::Handle handle, ReadCallback callback) = 0;

  // Sends an ATT Read Blob request with the requested attribute |handle| and
  // returns the result value in |callback|. This can be called multiple times
  // to read the value of a characteristic that is larger than the ATT_MTU.
  // (Vol 3, Part G, 4.8.3)
  virtual void ReadBlobRequest(att::Handle handle, uint16_t offset,
                               ReadCallback callback) = 0;

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

  // Sends an ATT Write Command with the requested |handle| and |value|. This
  // should only be used with characteristics that support the "Write Without
  // Response" property.
  virtual void WriteWithoutResponse(att::Handle handle,
                                    const common::ByteBuffer& value) = 0;

  // Assigns a callback that will be called when a notification or indication
  // PDU is received.
  using NotificationCallback =
      fit::function<void(bool indication,
                         att::Handle handle,
                         const common::ByteBuffer& value)>;
  virtual void SetNotificationHandler(NotificationCallback handler) = 0;
};

}  // namespace gatt
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GATT_CLIENT_H_
