// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <zircon/compiler.h>

#include "garnet/drivers/bluetooth/lib/common/cancelable_callback.h"
#include "garnet/drivers/bluetooth/lib/hci/acl_data_packet.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fragmenter.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_defs.h"
#include "garnet/drivers/bluetooth/lib/l2cap/recombiner.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace btlib {

namespace l2cap {

class ChannelManager;

namespace internal {

class ChannelImpl;
class LESignalingChannel;
class SignalingChannel;

// Represents a controller logical link. Each instance aids in mapping L2CAP
// channels to their corresponding controller logical link and vice versa.
// Instances are created and owned by a ChannelManager.
class LogicalLink final {
 public:
  LogicalLink(hci::ConnectionHandle handle,
              hci::Connection::LinkType type,
              hci::Connection::Role role,
              fxl::RefPtr<fxl::TaskRunner> task_runner,
              fxl::RefPtr<hci::Transport> hci);

  // When a logical link is destroyed it notifies all of its channels to close
  // themselves. Data packets will no longer be routed to the associated
  // channels.
  ~LogicalLink();

  // Opens the channel with |channel_id| over this logical link. See channel.h
  // for documentation on |rx_callback| and |closed_callback|. Returns nullptr
  // if a Channel for |channel_id| already exists.
  fbl::RefPtr<Channel> OpenFixedChannel(ChannelId channel_id);

  // Takes ownership of |packet| for PDU processing and routes it to its target
  // channel. This must be called on the HCI I/O thread.
  void HandleRxPacket(hci::ACLDataPacketPtr packet);

  // Sends a B-frame PDU out over the ACL data channel, where |payload| is the
  // B-frame information payload. |id| identifies the L2CAP channel that this
  // frame is coming from. This must be called on the creation thread.
  void SendBasicFrame(ChannelId id, const common::ByteBuffer& payload);

  // Assigns the link error callback to be invoked when a channel signals a link
  // error.
  void set_error_callback(std::function<void()> callback,
                          fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Returns task runner that this LogicalLink operates on.
  fxl::RefPtr<fxl::TaskRunner> task_runner() const { return task_runner_; }

  hci::Connection::LinkType type() const { return type_; }
  hci::Connection::Role role() const { return role_; }

  // Returns the LE signaling channel implementation or nullptr if this is not a
  // LE-U link.
  LESignalingChannel* le_signaling_channel() const;

 private:
  friend class ChannelImpl;

  bool AllowsFixedChannel(ChannelId id);

  // Called by ChannelImpl::Deactivate(). Removes the channel from the given
  // link.
  void RemoveChannel(Channel* chan);

  // Called by ChannelImpl::SignalLinkError().
  void SignalError();

  // Notifies and closes all open channels on this link. Called by the
  // destructor.
  void Close();

  fxl::RefPtr<hci::Transport> hci_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // Information about the underlying controller logical link.
  hci::ConnectionHandle handle_;
  hci::Connection::LinkType type_;
  hci::Connection::Role role_;

  std::function<void()> link_error_cb_;
  fxl::RefPtr<fxl::TaskRunner> link_error_runner_;

  // Owns and manages the L2CAP signaling channel on this logical link.
  // Depending on |type_| this will either implement the LE or BR/EDR signaling
  // commands.
  std::unique_ptr<SignalingChannel> signaling_channel_;

  // Fragmenter and Recombiner are always accessed on the L2CAP thread.
  Fragmenter fragmenter_;
  Recombiner recombiner_;

  // Channels that were created on this link. Channels notify the link for
  // removal when deactivated.
  using ChannelMap = std::unordered_map<ChannelId, fbl::RefPtr<ChannelImpl>>;
  ChannelMap channels_;

  // Stores packets that have been received on a currently closed channel. We
  // buffer these for fixed channels so that the data is available when the
  // channel is opened.
  using PendingPduMap = std::unordered_map<ChannelId, std::list<PDU>>;
  PendingPduMap pending_pdus_;

  fxl::WeakPtrFactory<LogicalLink> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogicalLink);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
