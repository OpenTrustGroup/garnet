// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_
#define GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_

#include <unordered_map>

#include <bitmap/rle-bitmap.h>
#include <fuchsia/cpp/virtualization.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>

// An endpoint that represents the host. Specifically this endpoint will
// exposes an interface for registering listeners on a per-port basis.
class HostVsockEndpointImpl : public virtualization::HostVsockConnector,
                              public virtualization::HostVsockEndpoint {
 public:
  HostVsockEndpointImpl();
  ~HostVsockEndpointImpl();

  void AddBinding(
      fidl::InterfaceRequest<virtualization::HostVsockEndpoint> request);

  fidl::InterfaceHandle<virtualization::HostVsockConnector> ConnectorNewBinding() {
    return connector_binding_.NewBinding();
  }

  // |virtualization::HostVsockConnector|
  void Connect(
      uint32_t src_cid, uint32_t src_port, uint32_t cid, uint32_t port,
      virtualization::HostVsockConnector::ConnectCallback callback) override;

  // |virtualization::HostVsockConnector|
  void DisConnect(
      uint32_t src_cid, uint32_t src_port, uint32_t cid, uint32_t port,
      DisConnectCallback callback) override;

  // |virtualization::HostVsockEndpoint|
  void Listen(uint32_t port,
              fidl::InterfaceHandle<virtualization::HostVsockAcceptor> acceptor,
              ListenCallback callback) override;

  // |virtualization::HostVsockEndpoint|
  void Close(uint32_t port,
              CloseCallback callback) override;

 private:
  std::mutex mutex_;
  bitmap::RleBitmap port_bitmap_ __TA_GUARDED(mutex_);
  std::unordered_map<uint32_t, virtualization::HostVsockAcceptorPtr> listeners_ __TA_GUARDED(mutex_);

  fidl::BindingSet<virtualization::HostVsockEndpoint> bindings_;
  fidl::Binding<virtualization::HostVsockConnector> connector_binding_;
};

#endif  // GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_
