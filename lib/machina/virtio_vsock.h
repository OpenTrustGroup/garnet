// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
#define GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_

#include <unordered_map>
#include <unordered_set>

#include <fuchsia/guest/cpp/fidl.h>
#include <virtio/virtio_ids.h>
#include <virtio/vsock.h>
#include <zx/channel.h>
#include <zx/socket.h>

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace machina {

static constexpr uint16_t kVirtioVsockNumQueues = 3;

class VirtioVsock
    : public VirtioInprocessDevice<VIRTIO_ID_VSOCK, kVirtioVsockNumQueues,
                                   virtio_vsock_config_t>,
      public fuchsia::guest::GuestVsockEndpoint,
      public fuchsia::guest::GuestVsockAcceptor {
 public:
  VirtioVsock(component::StartupContext* context, const PhysMem&,
              async_dispatcher_t* dispatcher);

  uint32_t guest_cid() const;

  // Check whether a connection exists. The connection is identified by a local
  // tuple, local_cid/local_port, and a remote tuple, guest_cid/remote_port. The
  // local tuple identifies the host-side of the connection, and the remote
  // tuple identifies the guest-side of the connection.
  bool HasConnection(uint32_t local_cid, uint32_t local_port,
                     uint32_t remote_port) const;

  VirtioQueue* rx_queue() { return queue(0); }
  VirtioQueue* tx_queue() { return queue(1); }

  class Connection;
  class NullConnection;
  class SocketConnection;
  class ChannelConnection;

 private:
  struct ConnectionKey {
    // The host-side of the connection is represented by local_cid and
    // local_port.
    uint32_t local_cid;
    uint32_t local_port;
    // The guest-side of the connection is represented by guest_cid and
    // remote_port.
    uint32_t remote_port;
    bool operator==(const ConnectionKey& key) const {
      return local_cid == key.local_cid && local_port == key.local_port &&
             remote_port == key.remote_port;
    }
  };
  struct ConnectionHash {
    size_t operator()(const ConnectionKey& key) const {
      return ((static_cast<size_t>(key.local_cid) << 32) | key.local_port) ^
             (key.remote_port << 16);
    }
  };
  using ConnectionMap =
      std::unordered_map<ConnectionKey, std::unique_ptr<Connection>,
                         ConnectionHash>;
  using ConnectionSet = std::unordered_set<ConnectionKey, ConnectionHash>;

  using StreamFunc = void (VirtioVsock::*)(zx_status_t, uint16_t);
  template <StreamFunc F>
  class Stream {
   public:
    Stream(async_dispatcher_t* dispatcher, VirtioQueue* queue,
           VirtioVsock* device);

    zx_status_t WaitOnQueue();

   private:
    VirtioQueueWaiter waiter_;
  };

  // |fuchsia::guest::GuestVsockEndpoint|
  void SetContextId(
      uint32_t cid,
      fidl::InterfaceHandle<fuchsia::guest::HostVsockConnector> connector,
      fidl::InterfaceRequest<fuchsia::guest::GuestVsockAcceptor> acceptor)
      override;
  // |fuchsia::guest::GuestVsockAcceptor|
  void Accept(
      uint32_t src_cid, uint32_t src_port, uint32_t port, zx::handle handle,
      fuchsia::guest::GuestVsockAcceptor::AcceptCallback callback) override;
  void ConnectCallback(ConnectionKey key, zx_status_t status,
                       zx::handle handle);

  zx_status_t AddConnectionLocked(ConnectionKey key,
                                  std::unique_ptr<Connection> conn)
      __TA_REQUIRES(mutex_);
  Connection* GetConnectionLocked(ConnectionKey key) __TA_REQUIRES(mutex_);
  bool EraseOnErrorLocked(ConnectionKey key, zx_status_t status)
      __TA_REQUIRES(mutex_);
  void WaitOnQueueLocked(ConnectionKey key) __TA_REQUIRES(mutex_);

  void Mux(zx_status_t status, uint16_t index);
  void Demux(zx_status_t status, uint16_t index);

  async_dispatcher_t* const dispatcher_;
  Stream<&VirtioVsock::Mux> rx_stream_;
  Stream<&VirtioVsock::Demux> tx_stream_;

  // TODO(PD-117): Evaluate granularity of locking.
  mutable std::mutex mutex_;
  ConnectionMap connections_ __TA_GUARDED(mutex_);
  ConnectionSet readable_ __TA_GUARDED(mutex_);
  // NOTE(abdulla): We ignore the event queue, as we don't support VM migration.

  fidl::BindingSet<fuchsia::guest::GuestVsockEndpoint> endpoint_bindings_;
  fidl::BindingSet<fuchsia::guest::GuestVsockAcceptor> acceptor_bindings_;
  fuchsia::guest::HostVsockConnectorPtr connector_;
};

class VirtioVsock::Connection {
 public:
  Connection(async_dispatcher_t* dispatcher,
             fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
             fit::closure queue_callback);
  virtual ~Connection();
  virtual zx_status_t Init() = 0;

  uint32_t flags() const { return flags_; }
  uint16_t op() const {
    std::lock_guard<std::mutex> lock(op_update_mutex_);
    return op_;
  }

  zx_status_t Accept();
  void UpdateOp(uint16_t op);

  uint32_t PeerFree() const;
  // Read credit from the header.
  void ReadCredit(virtio_vsock_hdr_t* header);
  // Write credit to the header. If this function returns:
  // - ZX_OK, it indicates to the device that it was successful.
  // - ZX_ERR_UNAVAILABLE, it indicates to the device that there is no buffer
  //   available, and should wait for the connection to transmit data.
  // - Anything else, it indicates to the device the connection should be reset.
  virtual zx_status_t WriteCredit(virtio_vsock_hdr_t* header) = 0;

  virtual zx_status_t Shutdown(uint32_t flags) = 0;
  virtual zx_status_t Read(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                           VirtioDescriptor* desc, uint32_t* used) = 0;
  virtual zx_status_t Write(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                            VirtioDescriptor* desc) = 0;

  zx_status_t WaitOnTransmit(zx_status_t status);
  zx_status_t WaitOnReceive(zx_status_t status);

 protected:
  uint32_t flags_ = 0;
  uint32_t rx_cnt_ = 0;
  uint32_t tx_cnt_ = 0;
  uint32_t peer_buf_alloc_ = 0;
  uint32_t peer_fwd_cnt_ = 0;
  uint16_t op_ __TA_GUARDED(op_update_mutex_) = VIRTIO_VSOCK_OP_REQUEST;
  mutable std::mutex op_update_mutex_;

  async_dispatcher_t* dispatcher_;
  async::Wait rx_wait_;
  // We require a separate waiter due to the way zx_object_wait_async works with
  // ZX_WAIT_ASYNC_ONCE. If the handle was just created, its transmit buffer
  // would be empty, and therefore __ZX_OBJECT_WRITABLE would be asserted. When
  // invoking zx_object_wait_async, it will see that this signal is asserted and
  // create a port packet immediately and stop listening for further signals.
  // This masks our ability to listen for any other signals, therefore we split
  // waiting on __ZX_OBJECT_WRITABLE.
  async::Wait tx_wait_;

  fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback_;
  fit::closure queue_callback_;
};

class VirtioVsock::NullConnection final : public VirtioVsock::Connection {
 public:
  NullConnection() : Connection(nullptr, nullptr, nullptr) {}

  zx_status_t Init() override { return ZX_OK; }
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header) override { return ZX_OK; }

  zx_status_t Shutdown(uint32_t flags) override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Read(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                   VirtioDescriptor* desc, uint32_t* used) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Write(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                    VirtioDescriptor* desc) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
};

class VirtioVsock::SocketConnection final : public VirtioVsock::Connection {
 public:
  SocketConnection(
      zx::handle handle, async_dispatcher_t* dispatcher,
      fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
      fit::closure queue_callback);
  ~SocketConnection() override;

  zx_status_t Init() override;
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header) override;

  zx_status_t Shutdown(uint32_t flags) override;
  zx_status_t Read(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                   VirtioDescriptor* desc, uint32_t* used) override;
  zx_status_t Write(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                    VirtioDescriptor* desc) override;

 private:
  void OnReady(zx_status_t status, const zx_packet_signal_t* signal);

  // The number of bytes the guest expects us to have in our socket buffer.
  // This is the last credit_update sent minus any bytes we've received since
  // that update was sent.
  //
  // When this is 0 we'll need to send a CREDIT_UPDATE once buffer space has
  // been free'd so that the guest knows it can resume transmitting.
  size_t reported_buf_avail_ = 0;

  zx::socket socket_;
};

class VirtioVsock::ChannelConnection final : public VirtioVsock::Connection {
 public:
  ChannelConnection(
      zx::handle handle, async_dispatcher_t* dispatcher,
      fuchsia::guest::GuestVsockAcceptor::AcceptCallback accept_callback,
      fit::closure queue_callback);
  ~ChannelConnection() override;

  zx_status_t Init() override;
  zx_status_t WriteCredit(virtio_vsock_hdr_t* header) override;

  zx_status_t Shutdown(uint32_t flags) override;
  zx_status_t Read(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                   VirtioDescriptor* desc, uint32_t* used) override;
  zx_status_t Write(VirtioQueue* queue, virtio_vsock_hdr_t* header,
                    VirtioDescriptor* desc) override;

 private:
  void OnReady(zx_status_t status, const zx_packet_signal_t* signal);

  zx::channel channel_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_VSOCK_H_
