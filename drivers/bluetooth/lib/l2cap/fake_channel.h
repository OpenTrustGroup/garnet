// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fragmenter.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_defs.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace l2cap {
namespace testing {

// FakeChannel is a simple pass-through Channel implementation that is intended
// for L2CAP service level unit tests where data is transmitted over a L2CAP
// channel.
class FakeChannel : public Channel {
 public:
  FakeChannel(ChannelId id,
              hci::ConnectionHandle handle,
              hci::Connection::LinkType link_type);
  ~FakeChannel() override = default;

  // Routes the given data over to the rx handler as if it was received from the
  // controller.
  void Receive(const common::ByteBuffer& data);

  // Sets a delegate to notify when a frame was sent over the channel.
  using SendCallback =
      std::function<void(std::unique_ptr<const common::ByteBuffer>)>;
  void SetSendCallback(const SendCallback& callback,
                       fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Sets a callback to emulate the result of "SignalLinkError()". In
  // production, this callback is invoked by the link. This will be internally
  // set up for FakeChannels that are obtained from a
  // l2cap::testing::FakeLayer.
  void SetLinkErrorCallback(L2CAP::LinkErrorCallback callback,
                            fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Emulates channel closure.
  void Close();

  fxl::WeakPtr<FakeChannel> AsWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  // Activate() always fails if true.
  void set_activate_fails(bool value) { activate_fails_ = value; }

  // True if SignalLinkError() has been called.
  bool link_error() const { return link_error_; }

 protected:
  // Channel overrides:
  bool Activate(RxCallback rx_callback,
                ClosedCallback closed_callback,
                fxl::RefPtr<fxl::TaskRunner> task_runner) override;
  void Deactivate() override;
  void SignalLinkError() override;
  bool Send(std::unique_ptr<const common::ByteBuffer> sdu) override;

 private:
  Fragmenter fragmenter_;

  ClosedCallback closed_cb_;
  RxCallback rx_cb_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  SendCallback send_cb_;
  fxl::RefPtr<fxl::TaskRunner> send_task_runner_;

  L2CAP::LinkErrorCallback link_err_cb_;
  fxl::RefPtr<fxl::TaskRunner> link_err_runner_;

  bool activate_fails_;
  bool link_error_;

  fxl::WeakPtrFactory<FakeChannel> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeChannel);
};

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib
