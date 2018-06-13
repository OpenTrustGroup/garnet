// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"

namespace ree_agent {

ReeAgent::ReeAgent(uint32_t id, zx::channel ch, size_t max_msg_size)
    : message_channel_id_(id),
      message_channel_(fbl::move(ch)),
      max_message_size_(max_msg_size),
      state(State::STOP),
      async_(async_get_default()) {}

ReeAgent::~ReeAgent() {}

zx_status_t ReeAgent::Start() {
  fbl::AutoLock lock(&lock_);

  if (state != State::STOP) {
    return ZX_ERR_BAD_STATE;
  }

  if (read_buffer_ == nullptr) {
    read_buffer_.reset(new char[max_message_size_]);
    if (read_buffer_ == nullptr)
      return ZX_ERR_NO_MEMORY;
  }

  zx_signals_t signals = ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE;
  channel_wait_.set_object(message_channel_.get());
  channel_wait_.set_trigger(signals);

  zx_status_t status = channel_wait_.Begin(async_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start async wait channel. status=" << status;
    return status;
  }

  state = State::START;
  return ZX_OK;
};

zx_status_t ReeAgent::Stop() {
  fbl::AutoLock lock(&lock_);

  if (state != State::START) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status = channel_wait_.Cancel();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to cancel async wait channel. status=" << status;
    return status;
  }

  if (read_buffer_ != nullptr) {
    read_buffer_.reset(nullptr);
  }

  state = State::STOP;
  return ZX_OK;
};

void ReeAgent::OnChannelReady(async_t* async, async::WaitBase* wait,
                              zx_status_t status,
                              const zx_packet_signal_t* sig) {
  if (status != ZX_OK) {
    OnChannelClosed(status, "async wait on channel");
    return;
  }

  fbl::AutoLock lock(&lock_);

  void* buf = static_cast<void*>(read_buffer_.get());
  uint32_t actual;
  status = message_channel_.read(0, buf, max_message_size_, &actual, nullptr, 0,
                                 nullptr);

  // no message in channel
  if (status == ZX_ERR_SHOULD_WAIT) {
    status = wait->Begin(async);
    if (status != ZX_OK) {
      OnChannelClosed(status, "async wait on channel");
    }
    return;
  }

  if (status != ZX_OK) {
    OnChannelClosed(status, "read from channel");
    return;
  }

  status = HandleMessage(buf, actual);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to handle message " << status;
  }

  status = wait->Begin(async);
  if (status != ZX_OK) {
    OnChannelClosed(status, "wait on queue");
  }
};

void ReeAgent::OnChannelClosed(zx_status_t status, const char* action) {
  Stop();
  FXL_LOG(ERROR) << "Channel closed during step '" << action << "' (" << status
                 << ")";
}

// TODO(james): support message size > 64KB
zx_status_t ReeAgent::WriteMessage(void* buf, size_t size) {
  if (buf == nullptr)
    return ZX_ERR_INVALID_ARGS;

  if (size > max_message_size_) {
    FXL_LOG(ERROR) << "message size over max. message buffer size,"
                   << "size=0x" << std::hex << size;
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  return message_channel_.write(0, buf, size, NULL, 0);
}

}  // namespace ree_agent
