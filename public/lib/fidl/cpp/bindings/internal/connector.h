// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_CONNECTOR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_CONNECTOR_H_

#include <lib/async/cpp/auto_wait.h>
#include <zx/channel.h>

#include <functional>

#include "lib/fidl/cpp/bindings/message.h"

namespace f1dl {
namespace internal {

// The Connector class is responsible for performing read/write operations on a
// channel. It writes messages it receives through the MessageReceiver
// interface that it subclasses, and it forwards messages it reads through the
// MessageReceiver interface assigned as its incoming receiver.
//
// NOTE: Channel I/O is non-blocking.
//
class Connector : public MessageReceiver {
 public:
  // The Connector takes ownership of |channel|.
  explicit Connector(zx::channel channel);
  ~Connector() override;

  Connector(const Connector&) = delete;
  Connector& operator=(const Connector&) = delete;

  // Sets the receiver to handle messages read from the channel.  The
  // Connector will read messages from the channel regardless of whether or not
  // an incoming receiver has been set.
  void set_incoming_receiver(MessageReceiver* receiver) {
    incoming_receiver_ = receiver;
  }

  // Errors from incoming receivers will force the connector into an error
  // state, where no more messages will be processed. This method is used
  // during testing to prevent that from happening.
  void set_enforce_errors_from_incoming_receiver(bool enforce) {
    enforce_errors_from_incoming_receiver_ = enforce;
  }

  // Sets the error handler to receive notifications when an error is
  // encountered while reading from the channel or waiting to read from the
  // channel.
  void set_error_handler(std::function<void()> error_handler) {
    connection_error_handler_ = std::move(error_handler);
  }

  // Returns true if an error was encountered while reading from the channel or
  // waiting to read from the channel.
  bool encountered_error() const { return error_; }

  // Closes the channel, triggering the error state. Connector is put into a
  // quiescent state.
  void CloseChannel();

  // Releases the channel, not triggering the error state. Connector is put into
  // a quiescent state.
  zx::channel TakeChannel();

  // Is the connector bound to a channel?
  bool is_valid() const { return !!channel_; }

  // Waits for the next message on the channel, blocking until one arrives,
  // |deadline| is exceeded, or an error happens. Returns |true| if a message
  // has been delivered, |false| otherwise. When returning |false| closes the
  // channel, unless the reason for for returning |false| was
  // |ZX_ERR_SHOULD_WAIT| or |ZX_ERR_TIMED_OUT|. Use |encountered_error| to see
  // if an error occurred.
  bool WaitForIncomingMessageUntil(zx::time deadline);

  // MessageReceiver implementation:
  bool Accept(Message* message) override;

  zx_handle_t handle() const { return channel_.get(); }

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }

 private:
  async_wait_result_t OnHandleReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal_t* signal);

  // Returns false if |this| was destroyed during message dispatch.
  bool ReadSingleMessage(zx_status_t* read_result);

  void NotifyError();

  // Cancels any calls made to |waiter_|.
  void CancelWait();

  std::function<void()> connection_error_handler_;
  zx::channel channel_;
  async::AutoWait wait_;
  MessageReceiver* incoming_receiver_;

  bool error_;
  bool drop_writes_;
  bool enforce_errors_from_incoming_receiver_;

  // If non-null, this will be set to true when the Connector is destroyed.  We
  // use this flag to allow for the Connector to be destroyed as a side-effect
  // of dispatching an incoming message.
  bool* destroyed_flag_;
};

}  // namespace internal
}  // namespace f1dl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_CONNECTOR_H_
