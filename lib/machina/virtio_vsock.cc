// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_vsock.h"

#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/functional/auto_call.h"

#include <zircon/syscalls/object.h>
#include <zircon/types.h>

namespace machina {

constexpr uint64_t kGuestCid = virtualization::DEFAULT_GUEST_CID;

template <VirtioVsock::StreamFunc F>
VirtioVsock::Stream<F>::Stream(async_t* async,
                               VirtioQueue* queue,
                               VirtioVsock* vsock)
    : waiter_(async, queue, fbl::BindMember(vsock, F)) {
  FXL_LOG(INFO) << "VirtioVsock::Stream::Stream";
}

template <VirtioVsock::StreamFunc F>
VirtioVsock::Stream<F>::~Stream() {
  FXL_LOG(INFO) << "VirtioVsock::Stream::~Stream";
}

template <VirtioVsock::StreamFunc F>
zx_status_t VirtioVsock::Stream<F>::WaitOnQueue() {
  return waiter_.Begin();
}

VirtioVsock::Connection::Connection(
    async_t* async,
    virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
    fxl::Closure queue_callback)
    : async_(async),
      accept_callback_(std::move(accept_callback)),
      queue_callback_(std::move(queue_callback)) {
  FXL_LOG(INFO) << "VirtioVsock::Connection::Connection";
}

VirtioVsock::Connection::~Connection() {
  if (rx_wait_.is_pending()) {
    rx_wait_.Cancel(async_);
  }
  if (tx_wait_.is_pending()) {
    tx_wait_.Cancel(async_);
  }
  FXL_LOG(INFO) << "VirtioVsock::Connection::~Connection";
  if (accept_callback_) {
    accept_callback_(ZX_ERR_CONNECTION_REFUSED);
  }
}

zx_status_t VirtioVsock::Connection::Accept() {
  if (accept_callback_) {
    UpdateOp(VIRTIO_VSOCK_OP_RW);
    accept_callback_(ZX_OK);
    accept_callback_ = nullptr;
    return WaitOnReceive(ZX_OK);
  } else {
    UpdateOp(VIRTIO_VSOCK_OP_RST);
    return ZX_OK;
  }
}

void VirtioVsock::Connection::UpdateOp(uint16_t new_op) {
  std::lock_guard<std::mutex> lock(op_update_mutex_);

  if (new_op == op_) {
    return;
  }

  switch (new_op) {
    case VIRTIO_VSOCK_OP_SHUTDOWN:
    case VIRTIO_VSOCK_OP_RST:
      op_ = new_op;
      return;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      if (op_ == VIRTIO_VSOCK_OP_RW || op_ == VIRTIO_VSOCK_OP_RESPONSE) {
        op_ = new_op;
        return;
      }
      if (op_ == VIRTIO_VSOCK_OP_RESPONSE) {
        FXL_LOG(ERROR) << "Dropping credit update in RESPONSE state";
        return;
      }
      FXL_LOG(ERROR) << "Invalid credit operation, current state: " << op_;
      break;
    case VIRTIO_VSOCK_OP_RW:
      switch (op_) {
        case VIRTIO_VSOCK_OP_SHUTDOWN:
          if ((flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) != VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
            op_ = new_op;
            return;
          }
        case VIRTIO_VSOCK_OP_RESPONSE:
        case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
          op_ = new_op;
          return;
      }
      break;
    case VIRTIO_VSOCK_OP_RESPONSE:
      if (op_ == VIRTIO_VSOCK_OP_REQUEST) {
        op_ = new_op;
        return;
      }
      break;
    case VIRTIO_VSOCK_OP_REQUEST:
    default:
      break;
  }

  FXL_LOG_WARN_RATELIMITED() << "Vsock invalid state transition: from=" << op_
                             << ", to=" << new_op << ", force reset to RST";
  op_ = VIRTIO_VSOCK_OP_RST;
  WaitOnReceive(ZX_ERR_STOP);
  WaitOnTransmit(ZX_ERR_STOP);
}

uint32_t VirtioVsock::Connection::PeerFree() const {
  const uint32_t inflight = (tx_cnt_ >= peer_fwd_cnt_) ? (tx_cnt_ - peer_fwd_cnt_) : 0;
  return (peer_buf_alloc_ >= inflight) ? (peer_buf_alloc_ - inflight) : 0;
}

void VirtioVsock::Connection::ReadCredit(virtio_vsock_hdr_t* header) {
  SetCredit(header->buf_alloc, header->fwd_cnt);
}

void VirtioVsock::Connection::SetCredit(uint32_t buf_alloc, uint32_t fwd_cnt) {
  peer_buf_alloc_ = buf_alloc;
  peer_fwd_cnt_ = fwd_cnt;
}

static zx_status_t wait(async_t* async, async::Wait* wait, zx_status_t status) {
  if (status == ZX_ERR_SHOULD_WAIT) {
    status = ZX_OK;
  }
  if (status == ZX_OK) {
    if (wait->has_handler() && !wait->is_pending()) {
      status = wait->Begin(async);
    }
  }
  if (status != ZX_OK) {
    if (status != ZX_ERR_STOP) {
      FXL_LOG(ERROR) << "Failed to wait on socket " << status;
    }
    if (status != ZX_ERR_ALREADY_EXISTS) {
      wait->Cancel(async);
    }
  }
  return status;
}

zx_status_t VirtioVsock::Connection::WaitOnTransmit(zx_status_t status) {
  return wait(async_, &tx_wait_, status);
}

zx_status_t VirtioVsock::Connection::WaitOnReceive(zx_status_t status) {
  return wait(async_, &rx_wait_, status);
}

VirtioVsock::SocketConnection::SocketConnection(
    zx::handle handle,
    async_t* async,
    virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
    fxl::Closure queue_callback)
    : Connection(async, std::move(accept_callback), std::move(queue_callback)),
      socket_(std::move(handle)) {
  FXL_LOG(INFO) << "VirtioVsock::SocketConnection::SocketConnection";
}

VirtioVsock::SocketConnection::~SocketConnection() {
  // We must cancel the async wait before the socket is destroyed.
  FXL_LOG(INFO) << "VirtioVsock::SocketConnection::~SocketConnection";
  if (rx_wait_.is_pending()) {
    rx_wait_.Cancel(async_);
  }
  if (tx_wait_.is_pending()) {
    tx_wait_.Cancel(async_);
  }
  socket_.reset();
}

zx_status_t VirtioVsock::SocketConnection::Init() {
  rx_wait_.set_object(socket_.get());
  rx_wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED |
                       ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED);
  rx_wait_.set_handler([this](async_t* async, zx_status_t status,
                              const zx_packet_signal_t* signal) {
    OnReady(status, signal);
    return ASYNC_WAIT_FINISHED;
  });

  tx_wait_.set_object(socket_.get());
  tx_wait_.set_trigger(ZX_SOCKET_WRITABLE);
  tx_wait_.set_handler([this](async_t* async, zx_status_t status,
                              const zx_packet_signal_t* signal) {
    OnReady(status, signal);
    return ASYNC_WAIT_FINISHED;
  });

  return WaitOnReceive(ZX_OK);
}

void VirtioVsock::SocketConnection::OnReady(
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on socket " << status;
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE && PeerFree() > 0) {
    queue_callback_();
    return;
  }

  if (signal->observed & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED |
                          ZX_SOCKET_WRITE_DISABLED)) {
    zx_signals_t signals = rx_wait_.trigger();
    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
      flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
      rx_wait_.set_trigger(signals & ~ZX_SOCKET_PEER_CLOSED);
    } else {
      if (signal->observed & ZX_SOCKET_READ_DISABLED &&
          !(flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV)) {
        UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
        flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV;
        rx_wait_.set_trigger(signals & ~ZX_SOCKET_READ_DISABLED);
      }
      if (signal->observed & ZX_SOCKET_WRITE_DISABLED &&
          !(flags_ & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND)) {
        UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
        flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND;
        rx_wait_.set_trigger(signals & ~ZX_SOCKET_WRITE_DISABLED);
      }
    }
    queue_callback_();
    return;
  }

  if (reported_buf_avail_ == 0 && signal->observed & ZX_SOCKET_WRITABLE) {
    UpdateOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
    queue_callback_();
  }
}

zx_status_t VirtioVsock::SocketConnection::WriteCredit(
    virtio_vsock_hdr_t* header) {
  size_t max = 0;
  zx_status_t status =
      socket_.get_property(ZX_PROP_SOCKET_TX_BUF_MAX, &max, sizeof(max));
  if (status != ZX_OK) {
    return status;
  }
  size_t used = 0;
  status =
      socket_.get_property(ZX_PROP_SOCKET_TX_BUF_SIZE, &used, sizeof(used));
  if (status != ZX_OK) {
    return status;
  }
  header->buf_alloc = max;
  header->fwd_cnt = rx_cnt_ - used;
  reported_buf_avail_ = max - used;
  return reported_buf_avail_ != 0 ? ZX_OK : ZX_ERR_UNAVAILABLE;
}

zx_status_t VirtioVsock::SocketConnection::Shutdown(uint32_t flags) {
  uint32_t shutdown_flags =
      (flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV ? ZX_SOCKET_SHUTDOWN_READ : 0) |
      (flags & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND ? ZX_SOCKET_SHUTDOWN_WRITE : 0);
  return socket_.write(shutdown_flags, nullptr, 0, nullptr);
}

static zx_status_t setup_desc_chain(VirtioQueue* queue,
                                    virtio_vsock_hdr_t* header,
                                    virtio_desc_t* desc) {
  desc->addr = header + 1;
  desc->len -= sizeof(*header);
  if (desc->len == 0 && desc->has_next) {
    return queue->ReadDesc(desc->next, desc);
  }
  return ZX_OK;
}

zx_status_t VirtioVsock::SocketConnection::Read(VirtioQueue* queue,
                                                virtio_vsock_hdr_t* header,
                                                virtio_desc_t* desc,
                                                uint32_t* used) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    size_t len = std::min(desc->len, PeerFree());
    size_t actual;
    status = socket_.read(0, desc->addr, len, &actual);
    if (status != ZX_OK) {
      break;
    }

    *used += actual;
    tx_cnt_ += actual;
    if (PeerFree() == 0 || !desc->has_next || actual < desc->len) {
      break;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  header->len = *used;
  return status;
}

zx_status_t VirtioVsock::SocketConnection::Write(VirtioQueue* queue,
                                                 virtio_vsock_hdr_t* header,
                                                 virtio_desc_t* desc) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    uint32_t len = std::min(desc->len, header->len);
    size_t actual;
    status = socket_.write(0, desc->addr, len, &actual);
    rx_cnt_ += actual;
    header->len -= actual;
    if (status != ZX_OK || actual < len) {
      UpdateOp(VIRTIO_VSOCK_OP_RST);
      return ZX_OK;
    }

    reported_buf_avail_ -= actual;
    if (reported_buf_avail_ == 0 || !desc->has_next || header->len == 0) {
      return ZX_OK;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  return status;
}

VirtioVsock::ChannelConnection::ChannelConnection(
    zx::handle handle,
    async_t* async,
    virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
    fxl::Closure queue_callback)
    : Connection(async, std::move(accept_callback), std::move(queue_callback)),
      channel_(std::move(handle)) {
  FXL_LOG(INFO) << "VirtioVsock::ChannelConnection::ChannelConnection";
}

VirtioVsock::ChannelConnection::~ChannelConnection() {
  // We must cancel the async wait before the channel is destroyed.
  FXL_LOG(INFO) << "VirtioVsock::ChannelConnection::~ChannelConnection";
  if (rx_wait_.is_pending()) {
    rx_wait_.Cancel(async_);
  }
  if (tx_wait_.is_pending()) {
    tx_wait_.Cancel(async_);
  }
  channel_.reset();
}

zx_status_t VirtioVsock::ChannelConnection::Init() {
  rx_wait_.set_object(channel_.get());
  rx_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  rx_wait_.set_handler(
      [this](async_t* async, zx_status_t status,
             const zx_packet_signal_t* signal) {
    OnReady(status, signal);
    return ASYNC_WAIT_FINISHED;
  });

  tx_wait_.set_object(channel_.get());
  tx_wait_.set_trigger(ZX_CHANNEL_WRITABLE);
  tx_wait_.set_handler(
      [this](async_t* async, zx_status_t status,
             const zx_packet_signal_t* signal) {
    OnReady(status, signal);
    return ASYNC_WAIT_FINISHED;
  });

  return WaitOnReceive(ZX_OK);
}

void VirtioVsock::ChannelConnection::OnReady(zx_status_t status,
                                             const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on channel " << status;
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE && PeerFree() > 0) {
    queue_callback_();
    return;
  }

  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    UpdateOp(VIRTIO_VSOCK_OP_SHUTDOWN);
    flags_ |= VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
    zx_signals_t signals = rx_wait_.trigger();
    rx_wait_.set_trigger(signals & ~ZX_CHANNEL_PEER_CLOSED);
    queue_callback_();
  }
}

zx_status_t VirtioVsock::ChannelConnection::WriteCredit(
    virtio_vsock_hdr_t* header) {
  constexpr size_t max = ZX_CHANNEL_MAX_MSG_BYTES;
  constexpr size_t used = 0;

  header->buf_alloc = max;
  header->fwd_cnt = rx_cnt_ - used;
  return ZX_OK;
}

zx_status_t VirtioVsock::ChannelConnection::Shutdown(uint32_t flags) {
  return ZX_OK;
}

zx_status_t VirtioVsock::ChannelConnection::Read(VirtioQueue* queue,
                                                 virtio_vsock_hdr_t* header,
                                                 virtio_desc_t* desc,
                                                 uint32_t* used) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    size_t len = std::min(desc->len, PeerFree());
    uint32_t actual;
    status = channel_.read(0, desc->addr, len, &actual, nullptr, 0, nullptr);
    if (status != ZX_OK) {
      if (status == ZX_ERR_SHOULD_WAIT ||
          (status == ZX_ERR_BUFFER_TOO_SMALL && desc->len > PeerFree())) {
        status = ZX_OK;
      } else {
        FXL_LOG(ERROR) << "Failed to read from channel " << status;
      }
      break;
    }

    *used += actual;
    tx_cnt_ += actual;
    if (PeerFree() == 0 || !desc->has_next) {
      break;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  header->len = *used;
  return status;
}

zx_status_t VirtioVsock::ChannelConnection::Write(VirtioQueue* queue,
                                                  virtio_vsock_hdr_t* header,
                                                  virtio_desc_t* desc) {
  zx_status_t status = setup_desc_chain(queue, header, desc);
  while (status == ZX_OK) {
    status = channel_.write(0, desc->addr, desc->len, nullptr, 0);
    rx_cnt_ += desc->len;
    header->len -= desc->len;
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write from channel " << status;
      UpdateOp(VIRTIO_VSOCK_OP_RST);
      return ZX_OK;
    }

    if (!desc->has_next || header->len == 0) {
      return ZX_OK;
    }

    status = queue->ReadDesc(desc->next, desc);
  }
  return status;
}

VirtioVsock::VirtioVsock(const PhysMem& phys_mem, async_t* async)
    : VirtioDeviceBase(phys_mem),
      async_(async),
      rx_stream_(async, rx_queue(), this),
      tx_stream_(async, tx_queue(), this) {
  config_.guest_cid = kGuestCid;
  FXL_LOG(INFO) << "VirtioVsock::VirtioVsock "
                << fxl::StringPrintf("guest_cid: %lu", config_.guest_cid);
}

VirtioVsock::~VirtioVsock() {
  FXL_LOG(INFO) << "VirtioVsock::~VirtioVsock";
}

uint32_t VirtioVsock::guest_cid() const {
  fbl::AutoLock lock(&config_mutex_);
  return config_.guest_cid;
}

bool VirtioVsock::HasConnection(uint32_t src_cid,
                                uint32_t src_port,
                                uint32_t dst_cid,
                                uint32_t dst_port) const {
  ConnectionKey key{.local_cid = dst_cid, .local_port = dst_port,
                    .remote_cid = src_cid, .remote_port = src_port};
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.find(key) != connections_.end();
}

void VirtioVsock::Start(
    fidl::InterfaceHandle<virtualization::HostVsockConnector> connector) {
  if (connector_.is_bound()) {
    FXL_LOG(ERROR) << "Vsock connector already bound, ignore duplicate Start";
    return;
  }
  FXL_CHECK(connector_.Bind(std::move(connector)) == ZX_OK);
  FXL_LOG(INFO) << "VirtioVsock::Start "
                << "vsock wait on tx queue";
  tx_stream_.WaitOnQueue();
}

void VirtioVsock::Stop(void) {
  FXL_LOG(INFO) << "VirtioVsock::Stop";
  FXL_LOG(INFO) << "ConnectClientNums= "<< ConnectClientNums.load();
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto iter = connections_.begin(); iter != connections_.end(); ) {
    auto curr = iter;
    iter++;

    const ConnectionKey& key = curr->first;
    auto& conn_ptr = curr->second;

    FXL_LOG(INFO) << "VirtioVsock::Stop"
      << fxl::StringPrintf(
             " local_cid=%u local_port=%u remote_cid=%u remote_port=%u",
             key.local_cid, key.local_port, key.remote_cid, key.remote_port);

    conn_ptr->Shutdown(VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH);

    connections_.erase(curr);
    ConnectClientNums.fetch_sub(1);
  }

  if (connector_.is_bound()) {
    connector_.Unbind();
  }
  FXL_LOG(INFO) << "isEmpty= "<< connections_.empty();
}

static std::unique_ptr<VirtioVsock::Connection> create_connection(
    zx::handle handle,
    async_t* async,
    virtualization::GuestVsockAcceptor::AcceptCallback accept_callback,
    fxl::Closure queue_callback) {
  zx_obj_type_t type = fsl::GetType(handle.get());
  switch (type) {
    case zx::socket::TYPE:
      return std::make_unique<VirtioVsock::SocketConnection>(
          std::move(handle), async, std::move(accept_callback),
          std::move(queue_callback));
    case zx::channel::TYPE:
      return std::make_unique<VirtioVsock::ChannelConnection>(
          std::move(handle), async, std::move(accept_callback),
          std::move(queue_callback));
    default:
      FXL_LOG(ERROR) << "Unexpected handle type " << type;
      return nullptr;
  }
}

void VirtioVsock::ConnectCallback(ConnectionKey key,
                                  zx_status_t status,
                                  zx::handle handle,
                                  uint32_t buf_alloc,
                                  uint32_t fwd_cnt) {
  auto new_conn =
      create_connection(std::move(handle), async_, nullptr, [this, key] {
        std::lock_guard<std::mutex> lock(mutex_);
        WaitOnQueueLocked(key);
      });
  if (!new_conn) {
    new_conn = std::make_unique<NullConnection>();
  }
  Connection* conn = new_conn.get();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    zx_status_t add_status = AddConnectionLocked(key, std::move(new_conn));
    if (add_status != ZX_OK) {
      return;
    }
    if (status != ZX_OK) {
      conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
      WaitOnQueueLocked(key);
      return;
    }
  }

  conn->UpdateOp(VIRTIO_VSOCK_OP_RESPONSE);
  status = conn->Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to setup connection " << status;
  }
  conn->SetCredit(buf_alloc, fwd_cnt);
}

zx_status_t VirtioVsock::AddConnectionLocked(ConnectionKey key,
                                             std::unique_ptr<Connection> conn) {
  bool inserted;
  std::tie(std::ignore, inserted) = connections_.emplace(key, std::move(conn));
  if (!inserted) {
    FXL_LOG(ERROR) << "Connection already exists";
    return ZX_ERR_ALREADY_EXISTS;
  }
  ConnectClientNums.fetch_add(1);
  WaitOnQueueLocked(key);
  return ZX_OK;
}

VirtioVsock::Connection* VirtioVsock::GetConnectionLocked(ConnectionKey key) {
  auto it = connections_.find(key);
  return it == connections_.end() ? nullptr : it->second.get();
}

bool VirtioVsock::EraseOnErrorLocked(ConnectionKey key, zx_status_t status) {
  if (status != ZX_OK) {
    connections_.erase(key);
  }
  return status != ZX_OK;
}

void VirtioVsock::WaitOnQueueLocked(ConnectionKey key) {
  zx_status_t status = rx_stream_.WaitOnQueue();
  if (EraseOnErrorLocked(key, status)) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
    return;
  }

  readable_.insert(key);
}

static virtio_vsock_hdr_t* get_header(VirtioQueue* queue,
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

static zx_status_t transmit(VirtioVsock::Connection* conn,
                            VirtioQueue* queue,
                            virtio_vsock_hdr_t* header,
                            virtio_desc_t* desc,
                            uint32_t* used) {
  switch (conn->op()) {
    case VIRTIO_VSOCK_OP_REQUEST:
      conn->UpdateOp(VIRTIO_VSOCK_OP_RESPONSE);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RESPONSE:
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      conn->UpdateOp(VIRTIO_VSOCK_OP_RW);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RW:
      return conn->Read(queue, header, desc, used);
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      header->flags = conn->flags();
      if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
        conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
      } else {
        conn->UpdateOp(VIRTIO_VSOCK_OP_RW);
      }
      return ZX_OK;
    default:
    case VIRTIO_VSOCK_OP_RST:
      header->op = VIRTIO_VSOCK_OP_RST;
      return ZX_ERR_STOP;
  }
}

void VirtioVsock::Mux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  bool index_valid = true;
  virtio_desc_t desc;
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto i = readable_.begin(); i != readable_.end(); ) {
    auto curr = i;
    i++;

    Connection* conn = GetConnectionLocked(*curr);
    if (conn == nullptr) {
      readable_.erase(curr);
      continue;
    }

    if (conn->op() == VIRTIO_VSOCK_OP_RW &&
        conn->flags() & VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV) {
      conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
      FXL_LOG(ERROR) << "Receive was shutdown";
    }

    if (!index_valid) {
      status = rx_queue()->NextAvail(&index);
      if (status != ZX_OK) {
        if (index_valid) {
          rx_queue()->Return(index, 0);
        }
        return;
      }
    }
    virtio_vsock_hdr_t* header = get_header(rx_queue(), index, &desc, true);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from read queue";
      continue;
    }
    *header = {
        .src_cid = curr->local_cid,
        .dst_cid = guest_cid(),
        .src_port = curr->local_port,
        .dst_port = curr->remote_port,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = conn->op(),
    };

    zx_status_t write_status = conn->WriteCredit(header);
    switch (write_status) {
      case ZX_OK:
        break;
      case ZX_ERR_UNAVAILABLE:
        status = conn->WaitOnTransmit(ZX_OK);
        break;
      default:
        conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
        FXL_LOG(ERROR) << "Failed to write credit " << write_status;
        break;
    }

    uint32_t used = 0;
    status = transmit(conn, rx_queue(), header, &desc, &used);
    rx_queue()->Return(index, used + sizeof(*header));
    index_valid = false;
    status = conn->WaitOnReceive(status);
    if (EraseOnErrorLocked(*curr, status)) {
      readable_.erase(curr);
      continue;
    }
  }

  if (index_valid) {
    FXL_LOG(ERROR) << "Mux called with no readable connections. Descriptor "
                   << "will be returned with 0 length";
    rx_queue()->Return(index, 0);
  }
}

static void set_shutdown(virtio_vsock_hdr_t* header) {
  header->op = VIRTIO_VSOCK_OP_SHUTDOWN;
  header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
}

static zx_status_t receive(VirtioVsock::Connection* conn,
                           VirtioQueue* queue,
                           virtio_vsock_hdr_t* header,
                           virtio_desc_t* desc,
                           VirtioVsock* vsock,
                           const VirtioVsock::ConnectionKey& key) {
  const uint32_t valid_shutdown_flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_RECV |
                                        VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND |
                                        VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
  if ((header->flags & ~valid_shutdown_flags) != 0) {
    FXL_LOG(ERROR) << "Invalid vsock shutdown flags: " << header->flags;
    conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
    return ZX_ERR_BAD_STATE;
  }

  switch (header->op) {
    case VIRTIO_VSOCK_OP_RESPONSE: {
      zx_status_t status = conn->Init();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to setup connection " << status;
        return status;
      }
      return conn->Accept();
    }
    case VIRTIO_VSOCK_OP_RW:
      return conn->Write(queue, header, desc);
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      return ZX_OK;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
      conn->UpdateOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
      return ZX_OK;
    case VIRTIO_VSOCK_OP_RST:
      return ZX_ERR_STOP;
    default:
      header->flags = VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      if (header->flags == VIRTIO_VSOCK_FLAG_SHUTDOWN_BOTH) {
        conn->UpdateOp(VIRTIO_VSOCK_OP_RST);
        return ZX_OK;
      } else if (header->flags != 0) {
        return conn->Shutdown(header->flags);
      } else {
        FXL_LOG(ERROR) << "Connection shutdown with no shutdown flags set";
        return ZX_ERR_BAD_STATE;
      }
  }
}

void VirtioVsock::Demux(zx_status_t status, uint16_t index) {
  if (status != ZX_OK) {
    return;
  }

  virtio_desc_t desc;
  std::lock_guard<std::mutex> lock(mutex_);
  do {
    auto free_desc =
        fxl::MakeAutoCall([this, index]() { tx_queue()->Return(index, 0); });
    auto header = get_header(tx_queue(), index, &desc, false);
    if (header == nullptr) {
      FXL_LOG(ERROR) << "Failed to get header from write queue";
      return;
    } else if (header->type != VIRTIO_VSOCK_TYPE_STREAM) {
      set_shutdown(header);
      FXL_LOG(WARNING) << "Vsock only STREAM type supported, drop unknown packet";
    }
    ConnectionKey key{
        .local_cid = static_cast<uint32_t>(header->dst_cid),
        .local_port = static_cast<uint32_t>(header->dst_port),
        .remote_cid = static_cast<uint32_t>(header->src_cid),
        .remote_port = static_cast<uint32_t>(header->src_port),
    };

    Connection* conn = GetConnectionLocked(key);
    if (header->op == VIRTIO_VSOCK_OP_REQUEST) {
      if (conn != nullptr) {
        set_shutdown(header);
        FXL_LOG(ERROR) << "Connection request for an existing connection";
      } else if (header->src_cid != guest_cid()) {
        FXL_LOG(ERROR) << "Source CID does not match guest CID";
        continue;
      } else if (connector_) {
        connector_->Connect(
            header->src_cid, header->src_port,
            header->dst_cid, header->dst_port,
            [this, key, buf_alloc = header->buf_alloc,
             fwd_cnt = header->fwd_cnt](zx_status_t status, zx::handle handle) {
              ConnectCallback(key, status, std::move(handle), buf_alloc,
                              fwd_cnt);
            });
        continue;
      }
    }

    if (conn == nullptr) {
      if (header->op == VIRTIO_VSOCK_OP_RST) {
        continue;
      }

      auto new_conn = std::make_unique<NullConnection>();
      conn = new_conn.get();
      status = AddConnectionLocked(key, std::move(new_conn));
      set_shutdown(header);
      FXL_LOG(ERROR) << "Connection does not exist";
    } else if (conn->op() == VIRTIO_VSOCK_OP_RW &&
               conn->flags() & VIRTIO_VSOCK_FLAG_SHUTDOWN_SEND) {
      set_shutdown(header);
      FXL_LOG(ERROR) << "Send was shutdown";
    }

    conn->ReadCredit(header);
    status = receive(conn, tx_queue(), header, &desc, this, key);
    switch (conn->op()) {
      case VIRTIO_VSOCK_OP_RST:
      case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
        WaitOnQueueLocked(key);
        break;
      default:
        status = conn->WaitOnTransmit(status);
        EraseOnErrorLocked(key, status);
        break;
    }
  } while (tx_queue()->NextAvail(&index) == ZX_OK);

  status = tx_stream_.WaitOnQueue();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on queue " << status;
  }
}

}  // namespace machina
