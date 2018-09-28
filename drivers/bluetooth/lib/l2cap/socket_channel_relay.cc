// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <utility>

#include <lib/async/default.h>
#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"

namespace btlib {
namespace l2cap {

namespace internal {

SocketChannelRelay::SocketChannelRelay(zx::socket socket,
                                       fbl::RefPtr<Channel> channel,
                                       DeactivationCallback deactivation_cb)
    : state_(RelayState::kActivating),
      socket_(std::move(socket)),
      channel_(channel),
      dispatcher_(async_get_default_dispatcher()),
      deactivation_cb_(std::move(deactivation_cb)),
      weak_ptr_factory_(this) {
  ZX_DEBUG_ASSERT(dispatcher_);
  ZX_DEBUG_ASSERT(socket_);
  ZX_DEBUG_ASSERT(channel_);

  // Note: binding |this| is safe, as BindWait() wraps the bound method inside
  // of a lambda which verifies that |this| hasn't been destroyed.
  BindWait(ZX_SOCKET_READABLE, "socket read waiter", &sock_read_waiter_,
           fit::bind_member(this, &SocketChannelRelay::OnSocketReadable));
  BindWait(ZX_SOCKET_WRITABLE, "socket write waiter", &sock_write_waiter_,
           fit::bind_member(this, &SocketChannelRelay::OnSocketWritable));
  BindWait(ZX_SOCKET_PEER_CLOSED, "socket close waiter", &sock_close_waiter_,
           fit::bind_member(this, &SocketChannelRelay::OnSocketClosed));
}

SocketChannelRelay::~SocketChannelRelay() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());

  if (state_ != RelayState::kDeactivated) {
    bt_log(DEBUG, "l2cap",
           "Deactivating relay for channel %u in dtor; will require Channel's "
           "mutex",
           channel_->id());
    Deactivate();
  }
}

bool SocketChannelRelay::Activate() {
  ZX_DEBUG_ASSERT(state_ == RelayState::kActivating);

  // Note: we assume that BeginWait() does not synchronously dispatch any
  // events. The wait handler will assert otherwise.
  if (!BeginWait("socket close waiter", &sock_close_waiter_)) {
    // Perhaps |dispatcher| is already stopped.
    return false;
  }

  if (!BeginWait("socket read waiter", &sock_read_waiter_)) {
    // Perhaps |dispatcher| is already stopped.
    return false;
  }

  const auto self = weak_ptr_factory_.GetWeakPtr();
  const auto channel_id = channel_->id();
  const bool activate_success = channel_->Activate(
      [self, channel_id](SDU sdu) {
        // Note: this lambda _may_ be invoked synchronously.
        if (self) {
          self->OnChannelDataReceived(std::move(sdu));
        } else {
          bt_log(DEBUG, "l2cap",
                 "Ignoring SDU received on destroyed relay (channel_id=%#.4x)",
                 channel_id);
        }
      },
      [self, channel_id] {
        if (self) {
          self->OnChannelClosed();
        } else {
          bt_log(
              DEBUG, "l2cap",
              "Ignoring channel closure on destroyed relay (channel_id=%#.4x)",
              channel_id);
        }
      },
      dispatcher_);
  if (!activate_success) {
    return false;
  }

  state_ = RelayState::kActivated;
  return true;
}

void SocketChannelRelay::Deactivate() {
  ZX_DEBUG_ASSERT(state_ != RelayState::kDeactivated);

  state_ = RelayState::kDeactivating;
  if (!socket_write_queue_.empty()) {
    bt_log(TRACE, "l2cap",
           "Dropping %zu SDUs from channel %u due to channel closure",
           socket_write_queue_.size(), channel_->id());
    socket_write_queue_.clear();
  }
  channel_->Deactivate();

  // We assume that UnbindAndCancelWait() will not trigger a re-entrant call
  // into Deactivate(). And the RelayIsDestroyedWhenDispatcherIsShutDown test
  // verifies that to be the case. (If we had re-entrant calls, a
  // ZX_DEBUG_ASSERT() in the lambda bound by BindWait() would cause an abort.)
  UnbindAndCancelWait(&sock_read_waiter_);
  UnbindAndCancelWait(&sock_write_waiter_);
  UnbindAndCancelWait(&sock_close_waiter_);
  socket_.reset();

  // Any further callbacks are bugs. Update state_, to help us detect
  // those bugs.
  state_ = RelayState::kDeactivated;
}

void SocketChannelRelay::DeactivateAndRequestDestruction() {
  Deactivate();
  if (deactivation_cb_) {
    // NOTE: deactivation_cb_ is expected to destroy |this|. Since |this|
    // owns deactivation_cb_, we move() deactivation_cb_ outside of |this|
    // before invoking the callback.
    auto moved_deactivation_cb = std::move(deactivation_cb_);
    moved_deactivation_cb(channel_->id());
  }
}

void SocketChannelRelay::OnSocketReadable(zx_status_t status) {
  ZX_DEBUG_ASSERT(state_ == RelayState::kActivated);
  if (!CopyFromSocketToChannel() ||
      !BeginWait("socket read waiter", &sock_read_waiter_)) {
    DeactivateAndRequestDestruction();
  }
}

void SocketChannelRelay::OnSocketWritable(zx_status_t status) {
  ZX_DEBUG_ASSERT(state_ == RelayState::kActivated);
  ZX_DEBUG_ASSERT(!socket_write_queue_.empty());
  ServiceSocketWriteQueue();
}

void SocketChannelRelay::OnSocketClosed(zx_status_t status) {
  ZX_DEBUG_ASSERT(state_ == RelayState::kActivated);
  DeactivateAndRequestDestruction();
}

void SocketChannelRelay::OnChannelDataReceived(SDU sdu) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  // Note: kActivating is deliberately permitted, as ChannelImpl::Activate()
  // will synchronously deliver any queued frames.
  ZX_DEBUG_ASSERT(state_ != RelayState::kDeactivated);

  if (state_ == RelayState::kDeactivating) {
    bt_log(TRACE, "l2cap",
           "Ignorning %s on socket for channel %u while deactivating", __func__,
           channel_->id());
    return;
  }

  socket_write_queue_.push_back(std::move(sdu));
  ServiceSocketWriteQueue();
}

void SocketChannelRelay::OnChannelClosed() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(state_ != RelayState::kActivating);
  ZX_DEBUG_ASSERT(state_ != RelayState::kDeactivated);

  if (state_ == RelayState::kDeactivating) {
    bt_log(TRACE, "l2cap",
           "Ignorning %s on socket for channel %u while deactivating", __func__,
           channel_->id());
    return;
  }

  ZX_DEBUG_ASSERT(state_ == RelayState::kActivated);
  if (!socket_write_queue_.empty()) {
    ServiceSocketWriteQueue();
  }
  DeactivateAndRequestDestruction();
}

bool SocketChannelRelay::CopyFromSocketToChannel() {
  // Subtle: we make the read buffer larger than the TX MTU, so that we can
  // detect truncated datagrams.
  const size_t read_buf_size = channel_->tx_mtu() + 1;

  // TODO(NET-1390): Consider yielding occasionally. As-is, we run the risk of
  // starving other SocketChannelRelays on the same |dispatcher| (and anyone
  // else on |dispatcher|), if a misbehaving process spams its L2CAP socket. And
  // even if starvation isn't an issue, latency/jitter might be.
  zx_status_t read_res;
  uint8_t read_buf[read_buf_size];
  do {
    size_t n_bytes_read = 0;
    read_res = socket_.read(0, read_buf, read_buf_size, &n_bytes_read);
    ZX_DEBUG_ASSERT_MSG(read_res == ZX_OK || read_res == ZX_ERR_SHOULD_WAIT ||
                            read_res == ZX_ERR_PEER_CLOSED,
                        "%s", zx_status_get_string(read_res));
    ZX_DEBUG_ASSERT_MSG(n_bytes_read <= read_buf_size,
                        "(n_bytes_read=%zu, read_buf_size=%zu)", n_bytes_read,
                        read_buf_size);
    if (read_res == ZX_ERR_SHOULD_WAIT) {
      bt_log(DEBUG, "l2cap", "Failed to read from socket for channel %u: %s",
             channel_->id(), zx_status_get_string(read_res));
      return true;
    }

    if (read_res == ZX_ERR_PEER_CLOSED) {
      bt_log(DEBUG, "l2cap", "Failed to read from socket for channel %u: %s",
             channel_->id(), zx_status_get_string(read_res));
      return false;
    }

    ZX_DEBUG_ASSERT(n_bytes_read > 0);
    if (n_bytes_read > channel_->tx_mtu()) {
      return false;
    }

    // TODO(NET-1391): For low latency and low jitter, IWBN to avoid allocating
    // dynamic memory on every read.
    bool write_success =
        channel_->Send(std::make_unique<common::DynamicByteBuffer>(
            common::BufferView(read_buf, n_bytes_read)));
    if (!write_success) {
      bt_log(DEBUG, "l2cap", "Failed to write %zu bytes to channel %u",
             n_bytes_read, channel_->id());
    }
  } while (read_res == ZX_OK);

  return true;
}

void SocketChannelRelay::ServiceSocketWriteQueue() {
  // TODO(NET-1477): Similarly to CopyFromSocketToChannel(), we may want to
  // consider yielding occasionally. The data-rate from the Channel into the
  // socket write queue should be bounded by PHY layer data rates, which are
  // much lower than the CPU's data processing throughput, so starvation
  // shouldn't be an issue. However, latency might be.
  zx_status_t write_res;
  do {
    ZX_DEBUG_ASSERT(!socket_write_queue_.empty());
    ZX_DEBUG_ASSERT(socket_write_queue_.front().is_valid());
    ZX_DEBUG_ASSERT(socket_write_queue_.front().length());

    const SDU& sdu = socket_write_queue_.front();
    PDU::Reader(&sdu).ReadNext(
        sdu.length(), [&](const common::ByteBuffer& pdu) {
          size_t n_bytes_written = 0;
          write_res =
              socket_.write(0, pdu.data(), pdu.size(), &n_bytes_written);
          ZX_DEBUG_ASSERT_MSG(write_res == ZX_OK ||
                                  write_res == ZX_ERR_SHOULD_WAIT ||
                                  write_res == ZX_ERR_PEER_CLOSED,
                              "%s", zx_status_get_string(write_res));
          if (write_res != ZX_OK) {
            ZX_DEBUG_ASSERT(n_bytes_written == 0);
            bt_log(SPEW, "l2cap",
                   "Failed to write %zu bytes to socket for channel %u: %s",
                   pdu.size(), channel_->id(), zx_status_get_string(write_res));
            return;
          }
          ZX_DEBUG_ASSERT(n_bytes_written == pdu.size());
        });
    if (write_res == ZX_OK) {
      // Subtle: We can't do this inside the lambda, because ReadNext requires
      // the callback to maintain the validity of the PDU.
      // TODO(NET-1483): Improve the interface with PDU::Reader, and update this
      // code.
      socket_write_queue_.pop_front();
    }
  } while (write_res == ZX_OK && !socket_write_queue_.empty());

  if (!socket_write_queue_.empty() &&
      !BeginWait("socket write waiter", &sock_write_waiter_)) {
    DeactivateAndRequestDestruction();
  }
}

void SocketChannelRelay::BindWait(zx_signals_t trigger, const char* wait_name,
                                  async::Wait* wait,
                                  fit::function<void(zx_status_t)> handler) {
  wait->set_object(socket_.get());
  wait->set_trigger(trigger);
  wait->set_handler([self = weak_ptr_factory_.GetWeakPtr(),
                     channel_id = channel_->id(), wait_name,
                     expected_wait = wait, handler = std::move(handler)](
                        async_dispatcher_t* actual_dispatcher,
                        async::WaitBase* actual_wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT_MSG(self, "(%s, channel_id=%u)", wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(actual_dispatcher == self->dispatcher_,
                        "(%s, channel_id=%u)", wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(actual_wait == expected_wait, "(%s, channel_id=%u)",
                        wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_CANCELED,
                        "(%s, channel_id=%u)", wait_name, channel_id);

    if (status == ZX_ERR_CANCELED) {  // Dispatcher is shutting down.
      bt_log(TRACE, "l2cap", "%s canceled on socket for channel %u", wait_name,
             channel_id);
      self->DeactivateAndRequestDestruction();
      return;
    }

    ZX_DEBUG_ASSERT_MSG(signal, "(%s, channel_id=%u)", wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(signal->trigger == expected_wait->trigger(),
                        "(%s, channel_id=%u)", wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(self->thread_checker_.IsCreationThreadCurrent(),
                        "(%s, channel_id=%u)", wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(self->state_ != RelayState::kActivating,
                        "(%s, channel_id=%u)", wait_name, channel_id);
    ZX_DEBUG_ASSERT_MSG(self->state_ != RelayState::kDeactivated,
                        "(%s, channel_id=%u)", wait_name, channel_id);

    if (self->state_ == RelayState::kDeactivating) {
      bt_log(TRACE, "l2cap",
             "Ignorning %s on socket for channel %u while deactivating",
             wait_name, channel_id);
      return;
    }
    handler(status);
  });
}

bool SocketChannelRelay::BeginWait(const char* wait_name, async::Wait* wait) {
  ZX_DEBUG_ASSERT(state_ != RelayState::kDeactivating);
  ZX_DEBUG_ASSERT(state_ != RelayState::kDeactivated);

  if (wait->is_pending()) {
    return true;
  }

  zx_status_t wait_res = wait->Begin(dispatcher_);
  ZX_DEBUG_ASSERT(wait_res == ZX_OK || wait_res == ZX_ERR_BAD_STATE);

  if (wait_res != ZX_OK) {
    bt_log(ERROR, "l2cap", "Failed to enable waiting on %s: ", wait_name,
           zx_status_get_string(wait_res));
    return false;
  }

  return true;
}

void SocketChannelRelay::UnbindAndCancelWait(async::Wait* wait) {
  ZX_DEBUG_ASSERT(state_ != RelayState::kActivating);
  ZX_DEBUG_ASSERT(state_ != RelayState::kDeactivated);
  zx_status_t cancel_res;
  wait->set_handler(nullptr);
  cancel_res = wait->Cancel();
  ZX_DEBUG_ASSERT_MSG(cancel_res == ZX_OK || cancel_res == ZX_ERR_NOT_FOUND,
                      "Cancel failed: %s", zx_status_get_string(cancel_res));
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
