// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_RECOMBINER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_RECOMBINER_H_

#include <endian.h>

#include <cstdint>

#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_defs.h"
#include "garnet/drivers/bluetooth/lib/l2cap/pdu.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {

// A Recombiner can be used to obtain complete L2CAP frames from received
// fragments. Incoming ACL data packets can be accumulated in a Recombiner.
//
// Each instance of Recombiner is intended to be used over a unique logical
// link. ACL data packets with different connection handles should not be added
// to the same Recombiner (the code will assert this in debug-mode).
//
// THREAD-SAFETY:
//
// This class is not thread-safe. External locking should be provided if an
// instance will be accessed on multiple threads.
class Recombiner final {
 public:
  Recombiner();

  // Returns true if packet is complete. This means that all HCI data fragments
  // for this packet have been received and Release() can be called to obtain
  // the packet.
  bool ready() const { return ready_; }

  // Returns true if no PDU is currently being built, otherwise a partial or
  // complete set of fragments have been accumulated.
  bool empty() const { return !pdu_; }

  // Appends the given ACL data fragment to this PDU. Returns true if the
  // fragment was accepted. Otherwise returns false; this either means that the
  // PDU was ready before the call or |fragment| did not pass validity checks
  // and was rejected.
  //
  // Takes ownership of |fragment| only in the success case. The contents will
  // not be moved in the case of failure.
  bool AddFragment(hci::ACLDataPacketPtr&& fragment);

  // Returns a complete PDU in |out_pdu| (overwriting its contents) if the
  // accumulated ACL data fragments form a complete L2CAP frame. Otherwise,
  // returns false.
  bool Release(PDU* out_pdu);

  // Drops the current packet. Once this method returns, Recombiner::empty()
  // will return true.
  void Drop();

 private:
  // If |fragment| is a valid first fragment, this initializes the internal
  // variables and makes this recombiner "non-empty". This does not append the
  // fragment to |pdu_|.
  bool ProcessFirstFragment(const hci::ACLDataPacket& fragment);

  bool ready_;
  size_t frame_length_;
  size_t cur_length_;

  // The PDU currently being constructed, if any.
  common::Optional<PDU> pdu_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Recombiner);
};

}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_RECOMBINER_H_
