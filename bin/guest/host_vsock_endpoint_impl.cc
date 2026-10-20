// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/host_vsock_endpoint_impl.h"

#include <lib/async/default.h>
#include <lib/fxl/logging.h>
#include "lib/fxl/strings/string_printf.h"

HostVsockEndpointImpl::HostVsockEndpointImpl()
    : connector_binding_(this) {
  FXL_LOG(INFO) << "HostVsockEndpointImpl::HostVsockEndpointImpl";
}

HostVsockEndpointImpl::~HostVsockEndpointImpl() {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& entry : listeners_) {
    auto& acceptor_ptr = entry.second;
    if (acceptor_ptr.is_bound()) {
      acceptor_ptr.Unbind();
    }
  }
  listeners_.clear();
  port_bitmap_.ClearAll();

  if (bindings_.size() > 0) {
    bindings_.CloseAll();
  }

  if (connector_binding_.is_bound()) {
    connector_binding_.Unbind();
  }

  FXL_LOG(INFO) << "HostVsockEndpointImpl destroyed";
}

void HostVsockEndpointImpl::AddBinding(
    fidl::InterfaceRequest<virtualization::HostVsockEndpoint> request) {
  bindings_.AddBinding(this, std::move(request));
}

void HostVsockEndpointImpl::Connect(
    uint32_t src_cid,
    uint32_t src_port,
    uint32_t cid,
    uint32_t port,
    virtualization::HostVsockConnector::ConnectCallback callback) {
  FXL_LOG(INFO) << "HostVsockEndpointImpl::Connect"
                << fxl::StringPrintf(" src_cid=%u src_port=%u dst_cid=%u dst_port=%u", src_cid, src_port, cid, port);
  if (cid == virtualization::HOST_CID) {
    // Guest to host connection.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = listeners_.find(port);
      if (it == listeners_.end()) {
        FXL_LOG(ERROR) << "No listener on port " << port;
        callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
        return;
      }
      FXL_LOG(INFO) << "HostVsockEndpointImpl::Connect"
                    << " Found listener on port " << port;
      it->second->Accept(src_cid, src_port, port, std::move(callback));
    }
  }
}

void HostVsockEndpointImpl::DisConnect(
    uint32_t src_cid,
    uint32_t src_port,
    uint32_t cid,
    uint32_t port,
    DisConnectCallback callback) {
  FXL_LOG(INFO) << "HostVsockEndpointImpl::DisConnect"
                << fxl::StringPrintf(" src_cid=%u src_port=%u dst_cid=%u dst_port=%u", src_cid, src_port, cid, port);
}

void HostVsockEndpointImpl::Listen(
    uint32_t port,
    fidl::InterfaceHandle<virtualization::HostVsockAcceptor> acceptor,
    ListenCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (port_bitmap_.GetOne(port)) {
    FXL_LOG(ERROR) << "Port " << port << " already bound";
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  bool inserted;
  virtualization::HostVsockAcceptorPtr acceptor_ptr = acceptor.Bind();
  acceptor_ptr.set_error_handler([this, port](void) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = listeners_.find(port);
    if (it != listeners_.end()) {
      port_bitmap_.ClearOne(port);
      listeners_.erase(port);
      FXL_LOG(INFO) << "disconnect Acceptor On port " << port;
    }
  });
  std::tie(std::ignore, inserted) =
      listeners_.emplace(port, std::move(acceptor_ptr));
  if (!inserted) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  port_bitmap_.SetOne(port);

  FXL_LOG(INFO) << "HostVsockEndpointImpl::Listen"
                << fxl::StringPrintf(": Add listener on port %d", port);
  callback(ZX_OK);
}

void HostVsockEndpointImpl::Close(
    uint32_t port,
    CloseCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if the port is currently being listened on
  auto it = listeners_.find(port);
  if (it == listeners_.end()) {
    FXL_LOG(ERROR) << "Port " << port << " is not bound to any listener";
    callback(ZX_ERR_NOT_FOUND);
    return;
  }

  if (it->second.is_bound()) {
    it->second.Unbind();
  }

  // For simplicity, we'll close any listener on this port
  // Clear the port from bitmap
  port_bitmap_.ClearOne(port);

  // Remove the listener from the map
  listeners_.erase(it);

  FXL_LOG(INFO) << "HostVsockEndpointImpl::Close"
                << fxl::StringPrintf(": Removed listener on port %d", port);

  callback(ZX_OK);
}
