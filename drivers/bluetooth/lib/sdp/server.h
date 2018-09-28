// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_SDP_SERVER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_SDP_SERVER_H_

#include <map>

#include <fbl/function.h>
#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>
#include <zx/socket.h>

#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "garnet/drivers/bluetooth/lib/l2cap/scoped_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/rfcomm.h"
#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"
#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"
#include "garnet/drivers/bluetooth/lib/sdp/service_record.h"

#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace sdp {

// The SDP server object owns the Service Database and all Service Records.
// Only one server is expected to exist per host.
// This object is not thread-safe.
// TODO(jamuraa): make calls thread-safe or ensure single-threadedness
class Server final {
 public:
  // A new SDP server, which starts with just a ServiceDiscoveryService record.
  // Registers itself with |l2cap| when created.
  explicit Server(fbl::RefPtr<l2cap::L2CAP> l2cap);
  ~Server();

  // Initialize a new SDP profile connection with |peer_id| on |channel|.
  // Returns false if the channel cannot be activated.
  bool AddConnection(fbl::RefPtr<l2cap::Channel> channel);

  // Given an incomplete ServiceRecord, register a service that will be made
  // available over SDP.  Takes ownership of |record|.
  // A non-zero ServiceHandle will be returned if the service was successfully
  // registered. Any service handle previously set in |record| is ignored and
  // overwritten.
  // |conn_cb| will be called for any connections made to the registered
  // service with a socket set to communicate on the connection and the protocol
  // descriptor list for the endpoint which was connected.
  using ConnectCallback =
      fit::function<void(zx::socket connection, const DataElement& protocol)>;
  ServiceHandle RegisterService(ServiceRecord record, ConnectCallback conn_cb);

  // Unregister a service from the database. Idempotent.
  // Returns |true| if a record was removed.
  bool UnregisterService(ServiceHandle handle);

 private:
  // Returns the next unused Service Handle, or 0 if none are available.
  ServiceHandle GetNextHandle();

  // Performs a Service Search, returning any service record that contains
  // all UUID from the |search_pattern|
  ServiceSearchResponse SearchServices(
      const std::unordered_set<common::UUID>& pattern) const;

  // Gets Service Attributes in the |attribute_ranges| from the service record
  // with |handle|.
  ServiceAttributeResponse GetServiceAttributes(
      ServiceHandle handle, const std::list<AttributeRange>& ranges) const;

  // Retrieves Service Attributes in the |attribute_ranges|, using the pattern
  // to search for the services that contain all UUIDs from the |search_pattern|
  ServiceSearchAttributeResponse SearchAllServiceAttributes(
      const std::unordered_set<common::UUID>& search_pattern,
      const std::list<AttributeRange>& attribute_ranges) const;

  // l2cap::channel callbacks
  void OnChannelClosed(const hci::ConnectionHandle& handle);
  void OnRxBFrame(const hci::ConnectionHandle& handle, const l2cap::SDU& sdu);

  // l2cap::RegisterService callback
  void OnChannelConnected(l2cap::PSM psm, fbl::RefPtr<l2cap::Channel> channel);

  // The L2CAP layer this server is running on.  Used to register callbacks for
  // the channels of services registered.
  fbl::RefPtr<l2cap::L2CAP> l2cap_;

  std::unordered_map<hci::ConnectionHandle, l2cap::ScopedChannel> channels_;
  std::unordered_map<ServiceHandle, ServiceRecord> records_;

  // Registered handles to sets of PSMs registered.
  std::unordered_map<ServiceHandle, std::unordered_set<l2cap::PSM>>
      record_psms_;

  // Registered PSMs to a callback to route a newly connected socket.
  std::unordered_map<l2cap::PSM, fit::function<void(zx::socket)>>
      psm_callbacks_;

  // The next available ServiceHandle.
  ServiceHandle next_handle_;

  // The service database state tracker.
  uint32_t db_state_ __UNUSED;

  fxl::WeakPtrFactory<Server> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace sdp
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_SDP_SERVER_H_
