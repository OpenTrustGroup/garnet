// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/hci/connection_parameters.h"
#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {
namespace internal {

// Implements the L2CAP LE signaling fixed channel.
class LESignalingChannel : public SignalingChannel {
 public:
  using ConnectionParameterUpdateCallback =
      std::function<void(const hci::LEPreferredConnectionParameters& params)>;

  LESignalingChannel(fbl::RefPtr<Channel> chan, hci::Connection::Role role);
  ~LESignalingChannel() override = default;

  // Sets a |callback| to be invoked when a Connection Parameter Update request
  // is received with the given parameters. LESignalingChannel will
  // automatically accept these parameters, however it is up to the
  // implementation of |callback| to apply them to the controller.
  //
  // This task will be posted onto the given |task_runner|.
  void set_conn_param_update_callback(
      ConnectionParameterUpdateCallback callback,
      fxl::RefPtr<fxl::TaskRunner> task_runner) {
    FXL_DCHECK(IsCreationThreadCurrent());
    FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(task_runner));
    conn_param_update_cb_ = std::move(callback);
    conn_param_update_runner_ = task_runner;
  }

 private:
  void OnConnParamUpdateReceived(const SignalingPacket& packet);

  // SignalingChannel override
  bool HandlePacket(const SignalingPacket& packet) override;

  ConnectionParameterUpdateCallback conn_param_update_cb_;
  fxl::RefPtr<fxl::TaskRunner> conn_param_update_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LESignalingChannel);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
