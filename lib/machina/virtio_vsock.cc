// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

#include <fbl/auto_call.h>

namespace machina {

VirtioVsock::Connection::~Connection() {
  if (acceptor) {
    acceptor(ZX_ERR_CONNECTION_REFUSED, zx::socket());
  }
}

template <VirtioVsock::StreamFunc F>
VirtioVsock::Stream<F>::Stream(async_t* async, VirtioQueue* queue,
                               VirtioVsock* vsock)
    : waiter_(async, queue, fit::bind_member(vsock, F)) {}

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::Stream<F>::WaitOnQueue() {
  return waiter_.Begin();
}

VirtioVsock::VirtioVsock(component::ApplicationContext* context,
                         const PhysMem& phys_mem, async_t* async)
    : VirtioDeviceBase(phys_mem),
      async_(async),
      rx_stream_(async, rx_queue(), this),
      tx_stream_(async, tx_queue(), this) {
  config_.guest_cid = 0;
  if (context) {
    context->outgoing().AddPublicService<fuchsia::guest::SocketEndpoint>(
        [this](fidl::InterfaceRequest<fuchsia::guest::SocketEndpoint> request) {
          endpoint_bindings_.AddBinding(this, std::move(request));
        });
  }
}

uint32_t VirtioVsock::guest_cid() const {
  fbl::AutoLock lock(&config_mutex_);
  return config_.guest_cid;
}

bool VirtioVsock::HasConnection(uint32_t src_cid, uint32_t src_port,
                                uint32_t dst_port) const {
  ConnectionKey key{src_cid, src_port, dst_port};
  fbl::AutoLock lock(&mutex_);
  return connections_.find(key) != connections_.end();
}

void VirtioVsock::SetContextId(
    uint32_t cid,
    fidl::InterfaceHandle<fuchsia::guest::SocketConnector> connector,
    fidl::InterfaceRequest<fuchsia::guest::SocketAcceptor> acceptor) {
  {
    fbl::AutoLock lock(&config_mutex_);
    config_.guest_cid = cid;
  }
  FXL_LOG(INFO) << "Assigned CID: " << cid;
  acceptor_bindings_.AddBinding(this, std::move(acceptor));
  FXL_CHECK(connector_.Bind(std::move(connector)) == ZX_OK);
  tx_stream_.WaitOnQueue();
  // Send transport reset
}

void VirtioVsock::Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
                         AcceptCallback callback) {
  if (HasConnection(src_cid, src_port, port)) {
    callback(ZX_ERR_ALREADY_BOUND, zx::socket());
    return;
  }

  ConnectionKey key{src_cid, src_port, port};
  auto conn = fbl::make_unique<Connection>();
  conn->op = VIRTIO_VSOCK_OP_REQUEST;
  conn->acceptor = std::move(callback);

  // From here on out the |conn| destructor will handle connection refusal upon
  // deletion.

  zx_status_t status =
      zx::socket::create(ZX_SOCKET_STREAM, &conn->socket, &conn->remote_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create connection socket " << status;
    return;
  }

  status = SetupConnection(key, conn.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to setup connection " << status;
    return;
  }

  fbl::AutoLock lock(&mutex_);
  status = AddConnectionLocked(key, std::move(conn));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add connection " << status;
    return;
  }
}

void VirtioVsock::ConnectCallback(ConnectionKey key, zx_status_t status,
                                  zx::socket socket) {
  auto new_conn = fbl::make_unique<Connection>();
  Connection* conn = new_conn.get();

  {
    fbl::AutoLock lock(&mutex_);
    AddConnectionLocked(key, std::move(new_conn));
  }

  if (status != ZX_OK) {
    conn->op = VIRTIO_VSOCK_OP_RST;
    return;
  }
  conn->socket = std::move(socket);
  conn->op = VIRTIO_VSOCK_OP_RESPONSE;
  status = SetupConnection(key, conn);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to setup connection " << status;
    return;
  }
}

zx_status_t VirtioVsock::SetupConnection(ConnectionKey key, Connection* conn) {
  conn->rx_wait.set_object(conn->socket.get());
  conn->rx_wait.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED |
                            ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);
  conn->rx_wait.set_handler([this, key](async_t* async, async::Wait* wait,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    OnSocketReady(async, wait, status, signal, key);
  });
  // We require a separate waiter due to the way zx_object_wait_async works with
  // ZX_WAIT_ASYNC_ONCE. If the socket was just created, it's transmit buffer
  // would be empty, and therefore ZX_SOCKET_WRITABLE would be asserted. When
  // invoke zx_object_wait_async, it will see that this signal is asserted and
  // create a port packet immediately and stop listening for further signals.
  // This masks our ability to listen for any other signals, therefore we split
  // waiting on ZX_SOCKET_WRITABLE.
  conn->tx_wait.set_object(conn->socket.get());
  conn->tx_wait.set_trigger(ZX_SOCKET_WRITABLE);
  conn->tx_wait.set_handler([this, key](async_t* async, async::Wait* wait,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    OnSocketReady(async, wait, status, signal, key);
  });

  zx_status_t status = conn->rx_wait.Begin(async_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    return status;
  }
  return ZX_OK;
}

zx_status_t VirtioVsock::AddConnectionLocked(ConnectionKey key,
                                             fbl::unique_ptr<Connection> conn) {
  bool inserted;
  std::tie(std::ignore, inserted) = connections_.emplace(key, std::move(conn));
  if (!inserted) {
    FXL_LOG(ERROR) << "Connection already exists";
    return ZX_ERR_ALREADY_EXISTS;
  }
  zx_status_t status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
    return status;
  }
  return ZX_OK;
}

VirtioVsock::Connection* VirtioVsock::GetConnectionLocked(ConnectionKey key) {
  auto it = connections_.find(key);
  return it == connections_.end() ? nullptr : it->second.get();
}

virtio_vsock_hdr_t* VirtioVsock::GetHeaderLocked(VirtioQueue* queue,
                                                 uint16_t index,
                                                 virtio_desc_t* desc,
                                                 bool writable) {
  zx_status_t status = queue->ReadDesc(index, desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor from queue " << status;
    return nullptr;
  }
  if (desc->writable != writable) {
    FXL_LOG(ERROR) << "Descriptor is not "
                   << (writable ? "writable" : "readable");
    return nullptr;
  }
  if (desc->len < sizeof(virtio_vsock_hdr_t)) {
    FXL_LOG(ERROR) << "Descriptor is too small";
    return nullptr;
  }
  return static_cast<virtio_vsock_hdr_t*>(desc->addr);
}

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::WaitOnQueueLocked(ConnectionKey key,
                                           ConnectionSet* keys,
                                           Stream<F>* stream) {
  keys->insert(key);
  return stream->WaitOnQueue();
}

void VirtioVsock::WaitOnSocketLocked(zx_status_t status, ConnectionKey key,
                                     async::Wait* wait) {
  if (status == ZX_ERR_SHOULD_WAIT) {
    status = ZX_OK;
  }
  if (status == ZX_OK && !wait->is_pending()) {
    status = wait->Begin(async_);
  }
  if (status != ZX_OK) {
    if (status != ZX_ERR_STOP) {
      FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    }
    if (status != ZX_ERR_ALREADY_EXISTS) {
      wait->Cancel();
      connections_.erase(key);
    }
  }
}

void VirtioVsock::OnSocketReady(async_t* async, async::Wait* wait,
                                zx_status_t status,
                                const zx_packet_signal_t* signal,
                                ConnectionKey key) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on socket " << status;
    return;
  }

  fbl::AutoLock lock(&mutex_);
  Connection* conn = GetConnectionLocked(key);
  if (conn == nullptr) {
    FXL_LOG(ERROR) << "Socket does not exist";
    return;
  }

  // If the socket has been partially or fully closed, wait on the Virtio
  // receive queue.
  if (signal->observed & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED |
                          ZX_SOCKET_WRITE_DISABLED)) {
    zx_signals_t signals = conn->rx_wait.trigger();
    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      // The peer closed the socket, therefore we move to sending a full
      // connection shutdown.
      conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
      conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_PEER_CLOSED);
    } else {
      if (signal->observed & ZX_SOCKET_READ_DISABLED &&
          !(conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV)) {
        // The peer disabled reading, therefore we move to sending a partial
        // connection shutdown.
        conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
        conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV;
        conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_READ_DISABLED);
      }
      if (signal->observed & ZX_SOCKET_WRITE_DISABLED &&
          !(conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND)) {
        // The peer disabled writing, therefore we move to sending a partial
        // connection shutdown.
        conn->op = VIRTIO_VSOCK_OP_SHUTDOWN;
        conn->flags |= VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND;
        conn->rx_wait.set_trigger(signals & ~ZX_SOCKET_WRITE_DISABLED);
      }
    }
    status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
  }

  // If the socket is readable and our peer has buffer space, wait on the
  // Virtio receive queue.
  if (signal->observed & ZX_SOCKET_READABLE && conn->peer_free() > 0) {
    status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
  }

  // If the socket is writable, wait on the Virtio transmit queue.
  if (signal->observed & ZX_SOCKET_WRITABLE) {
    status = WaitOnQueueLocked(key, &writable_, &tx_stream_);
  }

  if (status != ZX_OK) {
    status = wait->Begin(async);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed while waiting on socket " << status;
    }
  }
}

static zx_status_t set_credit(virtio_vsock_hdr_t* header, uint32_t rx_cnt,
                              const zx::socket& socket) {
  size_t max = 0;
  zx_status_t status =
      socket.get_property(ZX_PROP_SOCKET_TX_BUF_MAX, &max, sizeof(max));
  if (status != ZX_OK) {
    return status;
  }
  size_t used = 0;
  status = socket.get_property(ZX_PROP_SOCKET_TX_BUF_SIZE, &used, sizeof(used));
  if (status != ZX_OK) {
    return status;
  }

  header->buf_alloc = max;
  header->fwd_cnt = rx_cnt - used;
  return ZX_OK;
}

void VirtioVsock::Mux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  bool index_valid = true;
  fbl::AutoLock lock(&mutex_);
  for (auto i = readable_.begin(), end = readable_.end(); i != end;
       i = readable_.erase(i)) {
    Connection* conn = GetConnectionLocked(*i);
    if (conn == nullptr) {
      continue;
    }
    if (!index_valid) {
      status = rx_queue()->NextAvail(&index);
      if (status != ZX_OK) {
        return;
      }
    }
    uint32_t used = 0;
    zx_status_t status = SendMessageForConnectionLocked(*i, conn, index, &used);
    rx_queue()->Return(index, used);
    index_valid = false;
    WaitOnSocketLocked(status, *i, &conn->rx_wait);
  }
}

static bool op_requires_credit(uint32_t op) {
  switch (op) {
    case VIRTIO_VSOCK_OP_REQUEST:
    case VIRTIO_VSOCK_OP_RESPONSE:
    case VIRTIO_VSOCK_OP_RW:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
      return true;
    default:
      return false;
  }
}

zx_status_t VirtioVsock::SendMessageForConnectionLocked(
    const ConnectionKey& key, Connection* conn, uint16_t index,
    uint32_t* used) {
  virtio_desc_t desc;
  auto header = GetHeaderLocked(rx_queue(), index, &desc, true);
  if (header == nullptr) {
    FXL_LOG(ERROR) << "Failed to get header from read queue";
    return ZX_ERR_STOP;
  }
  *header = {
      .src_cid = key.local_cid,
      .src_port = key.local_port,
      .dst_cid = guest_cid(),
      .dst_port = key.remote_port,
      .type = VIRTIO_VSOCK_TYPE_STREAM,
      .op = conn->op,
  };
  *used = sizeof(*header);

  // If reading was shutdown, but we're still receiving a read request, send
  // a connection reset.
  if (conn->op == VIRTIO_VSOCK_OP_RW &&
      conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
    conn->op = VIRTIO_VSOCK_OP_RST;
    FXL_LOG(ERROR) << "Receive was shutdown";
  }

  if (op_requires_credit(conn->op)) {
    zx_status_t status = set_credit(header, conn->rx_cnt, conn->socket);
    if (status != ZX_OK) {
      conn->op = VIRTIO_VSOCK_OP_RST;
      FXL_LOG(ERROR) << "Failed to set credit";
    }
  }

  switch (conn->op) {
    case VIRTIO_VSOCK_OP_REQUEST: {
      // We are sending a connection request, therefore we move to waiting
      // for response.
      conn->op = VIRTIO_VSOCK_OP_RESPONSE;
      return WaitOnQueueLocked(key, &writable_, &tx_stream_);
    }
    case VIRTIO_VSOCK_OP_RESPONSE:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      // We are sending a response or credit update, therefore we move to ready
      // to read/write.
      conn->op = VIRTIO_VSOCK_OP_RW;
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RW: {
      // We are reading from the socket.
      desc.addr = header + 1;
      desc.len -= *used;
      zx_status_t status;
      do {
        size_t actual;
        size_t peer_free = conn->peer_free();
        size_t len = desc.len < peer_free ? desc.len : peer_free;
        status = conn->socket.read(0, desc.addr, len, &actual);
        if (status == ZX_OK) {
          *used += actual;
          conn->tx_cnt += actual;
          if (conn->peer_free() == 0) {
            break;
          }
        }
        if (status != ZX_OK || !desc.has_next || actual < desc.len) {
          break;
        }
        status = rx_queue()->ReadDesc(desc.next, &desc);
      } while (status == ZX_OK);
      header->len = *used - sizeof(*header);
      return status;
    }
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      header->flags = conn->flags;
      if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
        // We are sending a full connection shutdown, therefore we move to
        // waiting for a connection reset.
        conn->op = VIRTIO_VSOCK_OP_RST;
      } else {
        // One side of the connection is still active, therefore we move to
        // ready to read/write.
        conn->op = VIRTIO_VSOCK_OP_RW;
      }
      return WaitOnQueueLocked(key, &writable_, &tx_stream_);
    default:
    case VIRTIO_VSOCK_OP_RST:
      // We are sending a connection reset, therefore remove the connection.
      header->op = VIRTIO_VSOCK_OP_RST;
      return ZX_ERR_STOP;
  }
}

static void set_shutdown(virtio_vsock_hdr_t* header) {
  header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
  header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
}

static uint32_t shutdown_flags(uint32_t f) {
  return (f & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV ? ZX_SOCKET_SHUTDOWN_READ : 0) |
         (f & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND ? ZX_SOCKET_SHUTDOWN_WRITE : 0);
}

void VirtioVsock::Demux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  virtio_desc_t desc;
  fbl::AutoLock lock(&mutex_);
  do {
    auto free_desc =
        fbl::MakeAutoCall([this, index]() { tx_queue()->Return(index, 0); });
    auto header = GetHeaderLocked(tx_queue(), index, &desc, false);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from write queue";
      return;
    } else if (header->type != VIRTIO_VSOCK_TYPE_STREAM) {
      set_shutdown(header);
      FXL_LOG(ERROR) << "Only stream sockets are supported";
    }
    ConnectionKey key{
        static_cast<uint32_t>(header->dst_cid),
        header->dst_port,
        header->src_port,
    };

    Connection* conn = nullptr;
    if (header->op != VIRTIO_VSOCK_OP_REQUEST) {
      conn = GetConnectionLocked(key);
      if (conn == nullptr) {
        // Build a connection to send a connection reset.
        auto new_conn = fbl::make_unique<Connection>();
        conn = new_conn.get();
        status = AddConnectionLocked(key, std::move(new_conn));
        set_shutdown(header);
        FXL_LOG(ERROR) << "Connection does not exist";
      } else if (writable_.find(key) == writable_.end()) {
        // There was a write, but the socket is not ready. If the guest is
        // respecting the credit update messages this should not happen.
        FXL_LOG(ERROR) << "Received write to a socket that is not wriable!"
                       << "Data will be lost.";
        continue;
      } else if (conn->op == VIRTIO_VSOCK_OP_RW &&
                 conn->flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND) {
        // We are receiving a write, but send was shutdown.
        set_shutdown(header);
        FXL_LOG(ERROR) << "Send was shutdown";
      }
    }

    if (conn && op_requires_credit(header->op)) {
      conn->peer_fwd_cnt = header->fwd_cnt;
      conn->peer_buf_alloc = header->buf_alloc;
    }

    switch (header->op) {
      case VIRTIO_VSOCK_OP_REQUEST: {
        // We received a request for the guest to connect to an external CID.
        // If we don't have a socket connector then implicitly just refuse any
        // outbound connections. Otherwise send out a request for a socket
        // connection to the remote CID.
        if (connector_) {
          connector_->Connect(
              header->src_port, header->dst_cid, header->dst_port,
              [this, key](zx_status_t status, zx::socket socket) {
                ConnectCallback(key, status, std::move(socket));
              });
        } else {
          auto new_conn = fbl::make_unique<Connection>();
          conn = new_conn.get();
          status = AddConnectionLocked(key, std::move(new_conn));
          set_shutdown(header);
        }
        break;
      }
      case VIRTIO_VSOCK_OP_RESPONSE: {
        // The guest has accepted the connection request. Move the connection
        // into the RW state and let the connector know that the socket is
        // ready.
        //
        // If we don't have an acceptor or remote_socket (provisioned in Accept)
        // then this is a spurious response so reset the connection.
        if (conn->acceptor && conn->remote_socket) {
          conn->op = VIRTIO_VSOCK_OP_RW;
          conn->acceptor(ZX_OK, std::move(conn->remote_socket));
          conn->acceptor = nullptr;
          WaitOnSocketLocked(status, key, &conn->rx_wait);
        } else {
          conn->op = VIRTIO_VSOCK_OP_RST;
          status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
        }
        break;
      }
      case VIRTIO_VSOCK_OP_RW:
        // We are writing to the socket.
        desc.addr = header + 1;
        desc.len -= sizeof(*header);
        do {
          uint32_t len = std::min(desc.len, header->len);
          size_t actual;
          status = conn->socket.write(0, desc.addr, len, &actual);
          conn->rx_cnt += actual;
          header->len -= actual;
          if (status != ZX_OK) {
            // NOTE: this drops some bytes in this data payload. Since we send
            // credit update messages that inform that guest about available
            // buffer space in the socket we don't expect this to happen in
            // practice.
            //
            // It's possible we've hit other error conditions here (ex:
            // PEER_CLOSED) so we'll just remove the connection and update
            // status accordingly.
            FXL_LOG(ERROR) << "Failed to write to connection socket " << status
                           << ". Attempted to write  " << len
                           << "b but only wrote " << actual << "b.";
            writable_.erase(key);
            if (status == ZX_ERR_SHOULD_WAIT) {
              status = ZX_OK;
            }
            break;
          }
          if (!desc.has_next || header->len == 0) {
            break;
          }
          status = tx_queue()->ReadDesc(desc.next, &desc);
        } while (status == ZX_OK);
        break;
      case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        // Credit update is handled by common code above.
        break;
      case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        // We received a credit request, therefore we move to sending a credit
        // update.
        conn->op = VIRTIO_VSOCK_OP_CREDIT_UPDATE;
        status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
        break;
      case VIRTIO_VSOCK_OP_RST:
        // We received a connection reset, therefore remove the connection.
        status = ZX_ERR_STOP;
        break;
      default:
        header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      case VIRTIO_VSOCK_OP_SHUTDOWN:
        // We received a full connection shutdown, therefore we move to sending
        // a connection reset.
        if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
          conn->op = VIRTIO_VSOCK_OP_RST;
          status = WaitOnQueueLocked(key, &readable_, &rx_stream_);
        } else if (header->flags != 0) {
          uint32_t flags = shutdown_flags(header->flags);
          status = conn->socket.write(flags, nullptr, 0, nullptr);
        }
        break;
    }

    if (conn != nullptr && conn->socket.is_valid()) {
      WaitOnSocketLocked(status, key, &conn->tx_wait);
    }
  } while (tx_queue()->NextAvail(&index) == ZX_OK);

  status = tx_stream_.WaitOnQueue();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
  }
}

}  // namespace machina
