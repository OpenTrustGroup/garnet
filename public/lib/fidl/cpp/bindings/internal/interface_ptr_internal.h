// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_INTERFACE_PTR_INTERNAL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_INTERFACE_PTR_INTERNAL_H_

#include <zircon/assert.h>

#include <algorithm>  // For |std::swap()|.
#include <functional>
#include <memory>
#include <utility>

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/router.h"

namespace fidl {
namespace internal {

template <typename Interface>
class InterfacePtrState {
 public:
  InterfacePtrState()
      : proxy_(nullptr), router_(nullptr) {}

  ~InterfacePtrState() {
    // Destruction order matters here. We delete |proxy_| first, even though
    // |router_| may have a reference to it, so that destructors for any request
    // callbacks still pending can interact with the InterfacePtr.
    delete proxy_;
    delete router_;
  }

  InterfacePtrState(const InterfacePtrState&) = delete;
  InterfacePtrState& operator=(const InterfacePtrState&) = delete;

  Interface* instance() {
    ConfigureProxyIfNecessary();

    // This will be null if the object is not bound.
    return proxy_;
  }

  void Swap(InterfacePtrState* other) {
    using std::swap;
    swap(other->proxy_, proxy_);
    swap(other->router_, router_);
    handle_.swap(other->handle_);
  }

  void Bind(InterfaceHandle<Interface> info) {
    ZX_DEBUG_ASSERT(!proxy_);
    ZX_DEBUG_ASSERT(!router_);
    ZX_DEBUG_ASSERT(!(bool)handle_);
    ZX_DEBUG_ASSERT(info.is_valid());

    handle_ = info.TakeChannel();
  }

  bool WaitForResponseUntil(zx::time deadline) {
    ConfigureProxyIfNecessary();

    ZX_DEBUG_ASSERT(router_);
    return router_->WaitForIncomingMessageUntil(deadline);
  }

  // After this method is called, the object is in an invalid state and
  // shouldn't be reused.
  InterfaceHandle<Interface> Unbind() {
    return InterfaceHandle<Interface>(
        router_ ? router_->TakeChannel() : std::move(handle_));
  }

  bool is_bound() const { return (bool)handle_ || router_; }

  bool encountered_error() const {
    return router_ ? router_->encountered_error() : false;
  }

  void set_error_handler(std::function<void()> error_handler) {
    ConfigureProxyIfNecessary();

    ZX_DEBUG_ASSERT(router_);
    router_->set_error_handler(std::move(error_handler));
  }

  Router* router_for_testing() {
    ConfigureProxyIfNecessary();
    return router_;
  }

  // The underlying channel.
  const zx::channel& channel() const { return handle_; }

 private:
  using Proxy = typename Interface::Proxy_;

  void ConfigureProxyIfNecessary() {
    // The proxy has been configured.
    if (proxy_) {
      ZX_DEBUG_ASSERT(router_);
      return;
    }
    // The object hasn't been bound.
    if (!handle_) {
      return;
    }

    MessageValidatorList validators;
    validators.push_back(
        std::unique_ptr<MessageValidator>(new MessageHeaderValidator));
    validators.push_back(std::unique_ptr<MessageValidator>(
        new typename Interface::ResponseValidator_));

    router_ = new Router(std::move(handle_), std::move(validators));
    proxy_ = new Proxy(router_);
  }

  Proxy* proxy_;
  Router* router_;

  // |proxy_| and |router_| are not initialized until read/write with the
  // channel handle is needed. |handle_| is valid between the Bind() call
  // and the initialization of |proxy_| and |router_|.
  zx::channel handle_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_INTERFACE_PTR_INTERNAL_H_
