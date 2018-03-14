// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_ROUTER_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_ROUTER_H_

#include <map>

#include "lib/fidl/cpp/bindings/internal/connector.h"
#include "lib/fidl/cpp/bindings/internal/shared_data.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/message_validator.h"

namespace f1dl {
namespace internal {

// Router provides a way for sending messages over a channel, and re-routing
// response messages back to the sender.
class Router : public MessageReceiverWithResponder {
 public:
  Router(zx::channel channel, MessageValidatorList validators);
  ~Router() override;

  // Sets the receiver to handle messages read from the channel that do
  // not have the kMessageIsResponse flag set.
  void set_incoming_receiver(MessageReceiverWithResponderStatus* receiver) {
    incoming_receiver_ = receiver;
  }

  // Sets the error handler to receive notifications when an error is
  // encountered while reading from the channel or waiting to read from the
  // channel.
  void set_error_handler(std::function<void()> error_handler) {
    connector_.set_error_handler(std::move(error_handler));
  }

  // Returns true if an error was encountered while reading from the channel or
  // waiting to read from the channel.
  bool encountered_error() const { return connector_.encountered_error(); }

  // Is the router bound to a channel?
  bool is_valid() const { return connector_.is_valid(); }

  void CloseChannel() { connector_.CloseChannel(); }

  zx::channel TakeChannel() { return connector_.TakeChannel(); }

  // MessageReceiver implementation:
  bool Accept(Message* message) override;
  bool AcceptWithResponder(Message* message,
                           MessageReceiver* responder) override;

  // Blocks the current thread until the first incoming method call, i.e.,
  // either a call to a client method or a callback method, or |timeout|.
  // When returning |false| closes the channel, unless the reason for
  // for returning |false| was |ZX_ERR_SHOULD_WAIT| or
  // |ZX_ERR_TIMED_OUT|.
  // Use |encountered_error| to see if an error occurred.
  bool WaitForIncomingMessageUntil(zx::time deadline) {
    return connector_.WaitForIncomingMessageUntil(deadline);
  }

  // Sets this object to testing mode.
  // In testing mode:
  // - the object is more tolerant of unrecognized response messages;
  // - the connector continues working after seeing errors from its incoming
  //   receiver.
  void EnableTestingMode();

  zx_handle_t handle() const { return connector_.handle(); }

  // The underlying channel.
  const zx::channel& channel() const { return connector_.channel(); }

 private:
  typedef std::map<uint64_t, MessageReceiver*> ResponderMap;

  // This class is registered for incoming messages from the |Connector|.  It
  // simply forwards them to |Router::HandleIncomingMessages|.
  class HandleIncomingMessageThunk : public MessageReceiver {
   public:
    explicit HandleIncomingMessageThunk(Router* router);
    ~HandleIncomingMessageThunk() override;

    // MessageReceiver implementation:
    bool Accept(Message* message) override;

   private:
    Router* router_;
  };

  bool HandleIncomingMessage(Message* message);

  HandleIncomingMessageThunk thunk_;
  MessageValidatorList validators_;
  Connector connector_;
  SharedData<Router*> weak_self_;
  MessageReceiverWithResponderStatus* incoming_receiver_;
  ResponderMap responders_;
  uint64_t next_request_id_;
  bool testing_mode_;
};

}  // namespace internal
}  // namespace f1dl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_ROUTER_H_
