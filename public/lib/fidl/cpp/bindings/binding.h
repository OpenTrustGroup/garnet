// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_BINDING_H_
#define LIB_FIDL_CPP_BINDINGS_BINDING_H_

#include <zircon/assert.h>
#include <zx/channel.h>

#include <memory>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/router.h"

namespace fidl {

// Represents the binding of an interface implementation to a channel.
// When the |Binding| object is destroyed, the binding between the channel
// and the interface is torn down and the channel is closed, leaving the
// interface implementation in an unbound state.
//
// Example:
//
//   #include "foo.fidl.h"
//
//   class FooImpl : public Foo {
//    public:
//     explicit FooImpl(InterfaceRequest<Foo> request)
//         : binding_(this, std::move(request) {}
//
//     // Foo implementation here.
//
//    private:
//     Binding<Foo> binding_;
//   };
//
// The implementation type of the binding is also parameterized, allowing
// the use of smart pointer types such as |std::unique_ptr<>| to reference
// the implementation.
template <typename Interface, typename ImplPtr = Interface*>
class Binding {
 public:
  // Constructs an incomplete binding that will use the implementation |impl|.
  // The binding may be completed with a subsequent call to the |Bind| method.
  // Does not take ownership of |impl|, which must outlive the binding.
  explicit Binding(ImplPtr impl) : impl_(std::forward<ImplPtr>(impl)) {
    stub_.set_sink(&*this->impl());
  }

  // Constructs a completed binding of channel |handle| to implementation
  // |impl|. Does not take ownership of |impl|, which must outlive the binding.
  Binding(ImplPtr impl, zx::channel handle)
      : Binding(std::forward<ImplPtr>(impl)) {
    Bind(std::move(handle));
  }

  // Constructs a completed binding of |impl| to the channel endpoint in
  // |request|, taking ownership of the endpoint. Does not take ownership of
  // |impl|, which must outlive the binding.
  Binding(ImplPtr impl, InterfaceRequest<Interface> request)
      : Binding(std::forward<ImplPtr>(impl)) {
    Bind(request.TakeChannel());
  }

  // Tears down the binding, closing the channel and leaving the interface
  // implementation unbound.
  ~Binding() {}

  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;

  // Completes a binding by creating a new pair of channels, binding one end to
  // the previously specified implementation and returning the other end.
  InterfaceHandle<Interface> NewBinding() {
    InterfaceHandle<Interface> client;
    Bind(client.NewRequest().TakeChannel());
    return client;
  }

  // Completes a binding that was constructed with only an interface
  // implementation. Takes ownership of |handle| and binds it to the previously
  // specified implementation.
  void Bind(zx::channel handle) {
    ZX_DEBUG_ASSERT(!internal_router_);

    internal::MessageValidatorList validators;
    validators.push_back(std::unique_ptr<internal::MessageValidator>(
        new internal::MessageHeaderValidator));
    validators.push_back(std::unique_ptr<internal::MessageValidator>(
        new typename Interface::RequestValidator_));

    internal_router_.reset(
        new internal::Router(std::move(handle), std::move(validators)));
    internal_router_->set_incoming_receiver(&stub_);
    internal_router_->set_error_handler([this]() {
      if (connection_error_handler_)
        connection_error_handler_();
    });
  }

  // Completes a binding that was constructed with only an interface
  // implementation by removing the channel endpoint from |request| and
  // binding it to the previously specified implementation.
  void Bind(InterfaceRequest<Interface> request) {
    Bind(request.TakeChannel());
  }

  // Blocks the calling thread until either a call arrives on the previously
  // bound channel, the timeout is exceeded, or an error occurs. Returns
  // true if a method was successfully read and dispatched.
  bool WaitForMessage() {
    ZX_DEBUG_ASSERT(internal_router_);
    return internal_router_->WaitForIncomingMessageUntil(zx::time::infinite());
  }

  // Unbinds the underlying channel from this binding and returns it so it can
  // be used in another context, such as on another thread or with a different
  // implementation. Put this object into a state where it can be rebound to a
  // new channel.
  InterfaceRequest<Interface> Unbind() {
    auto request = InterfaceRequest<Interface>(internal_router_->TakeChannel());
    internal_router_.reset();
    return request;
  }

  // Sets an error handler that will be called if a connection error occurs on
  // the bound channel.
  void set_error_handler(std::function<void()> error_handler) {
    connection_error_handler_ = std::move(error_handler);
  }

  // Returns the interface implementation that was previously specified. Caller
  // does not take ownership.
  const ImplPtr& impl() const { return impl_; }

  // Indicates whether the binding has been completed (i.e., whether a channel
  // has been bound to the implementation).
  bool is_bound() const { return !!internal_router_; }

  // The underlying channel.
  const zx::channel& channel() const {
    ZX_DEBUG_ASSERT(is_bound());
    return internal_router_->channel();
  }

  // Exposed for testing, should not generally be used.
  internal::Router* internal_router() { return internal_router_.get(); }

 private:
  std::unique_ptr<internal::Router> internal_router_;
  typename Interface::Stub_ stub_;
  ImplPtr impl_;
  std::function<void()> connection_error_handler_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_BINDING_H_
