// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_LE_SIGNALING_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_LE_SIGNALING_CHANNEL_H_

#include "garnet/drivers/bluetooth/lib/hci/connection_parameters.h"
#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {
namespace internal {

// Implements the L2CAP LE signaling fixed channel.
class LESignalingChannel final : public SignalingChannel {
 public:
  using ConnectionParameterUpdateCallback =
      fit::function<void(const hci::LEPreferredConnectionParameters& params)>;

  LESignalingChannel(fbl::RefPtr<Channel> chan, hci::Connection::Role role);
  ~LESignalingChannel() override = default;

  // Sets a |callback| to be invoked when a Connection Parameter Update request
  // is received with the given parameters. LESignalingChannel will
  // automatically accept these parameters, however it is up to the
  // implementation of |callback| to apply them to the controller.
  //
  // This task will be posted onto the given |dispatcher|.
  void set_conn_param_update_callback(
      ConnectionParameterUpdateCallback callback, async_t* dispatcher) {
    FXL_DCHECK(IsCreationThreadCurrent());
    FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(dispatcher));
    conn_param_update_cb_ = std::move(callback);
    dispatcher_ = dispatcher;
  }

 private:
  void OnConnParamUpdateReceived(const SignalingPacket& packet);

  // SignalingChannel override
  void DecodeRxUnit(const SDU& sdu, const PacketDispatchCallback& cb) override;

  bool HandlePacket(const SignalingPacket& packet) override;

  ConnectionParameterUpdateCallback conn_param_update_cb_;
  async_t* dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LESignalingChannel);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_LE_SIGNALING_CHANNEL_H_
