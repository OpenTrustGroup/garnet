// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_CHANNEL_MANAGER_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_CHANNEL_MANAGER_H_

#include <tuple>
#include <unordered_map>

#include <fbl/ref_ptr.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/session.h"

namespace btlib {

namespace l2cap {
class L2CAP;
}  // namespace l2cap

namespace rfcomm {

// The main entry point for managing RFCOMM connections. ChannelManager has
// functions for connecting to remote RFCOMM channels and listening to
// connections on local channels.
//
// THREAD SAFETY:
//
// ChannelManager is not thread safe, and should only be accessed from the
// thread it was created on. ChannelManager dispatches its tasks to the default
// dispatcher of the thread it was created on.
class ChannelManager {
 public:
  // Returns a new ChannelManager. Returns nullptr if creation fails (e.g. if
  // registering the RFCOMM PSM with L2CAP fails). |l2cap| must be valid as long
  // as the created ChannelManager exists.
  static std::unique_ptr<ChannelManager> Create(l2cap::L2CAP* l2cap);

  // Registers |l2cap_channel| with RFCOMM. After this call, we will be able to
  // use OpenRemoteChannel() to get an RFCOMM channel multiplexed on top of this
  // L2CAP channel. Returns true on success, false otherwise.
  bool RegisterL2CAPChannel(fbl::RefPtr<l2cap::Channel> l2cap_channel);

  // This callback will be used to transfer ownership of a new RFCOMM Channel
  // object. It is used to deliver both incoming channels (initiated by the
  // remote) and outgoing channels. Failure will be indicated by a value of
  // kInvalidServerChannel as the ServerChannel, and a nullptr as the Channel
  // pointer.
  using ChannelOpenedCallback =
      fit::function<void(fbl::RefPtr<Channel>, ServerChannel)>;

  // Open an outgoing RFCOMM channel to the remote device represented by
  // |handle|. Registers an L2CAP channel to |handle| if necessary.
  void OpenRemoteChannel(hci::ConnectionHandle handle,
                         ServerChannel server_channel,
                         ChannelOpenedCallback channel_opened_cb,
                         async_dispatcher_t* dispatcher);

  // Reserve an incoming RFCOMM channel, and get its Server Channel. Any
  // incoming RFCOMM channels opened with this Server Channel will be
  // given to |cb|. Returns the Server Channel allocated on success, and
  // kInvalidServerChannel otherwise.
  ServerChannel AllocateLocalChannel(ChannelOpenedCallback cb,
                                     async_dispatcher_t* dispatcher);

 private:
  ChannelManager(l2cap::L2CAP* l2cap);

  // Calls the appropriate callback for |server_channel|, passing in
  // |rfcomm_channel|.
  void ChannelOpened(fbl::RefPtr<Channel> rfcomm_channel,
                     ServerChannel server_channel);

  // Holds callbacks for Server Channels allocated via AllocateLocalChannel.
  std::unordered_map<ServerChannel,
                     std::pair<ChannelOpenedCallback, async_dispatcher_t*>>
      server_channels_;

  // Maps open connections to open RFCOMM sessions.
  std::unordered_map<hci::ConnectionHandle, std::unique_ptr<Session>>
      handle_to_session_;

  // The dispatcher which ChannelManager uses to run its own tasks.
  async_dispatcher_t* dispatcher_;

  // A reference to the top-level L2CAP object, which creates and owns this
  // ChannelManager object. This reference is guaranteed to exist. Note that
  // using a RefPtr here would create a cyclical reference.
  l2cap::L2CAP* l2cap_;

  fxl::WeakPtrFactory<ChannelManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelManager);
};

}  // namespace rfcomm
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_CHANNEL_MANAGER_H_
