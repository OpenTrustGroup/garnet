// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>
#include <zx/channel.h>

namespace ree_agent {

static constexpr uint32_t kDefaultReservedSize = 256;
static constexpr uint32_t kDefaultHandleCapacity = 4;

class Message {
 public:
  Message(uint8_t* bytes, size_t byte_capacity, size_t bytes_reserved,
          zx_handle_t* handles, size_t handle_capacity)
      : bytes_reserved_(bytes_reserved),
        bytes_(bytes, byte_capacity),
        handles_(handles, handle_capacity) {}

  Message(const Message& other) = delete;
  Message& operator=(const Message& other) = delete;

  Message(Message&& other)
      : bytes_reserved_(other.bytes_reserved_),
        bytes_(std::move(other.bytes_)),
        handles_(std::move(other.handles_)) {
    other.bytes_reserved_ = 0u;
  }

  zx_status_t Read(zx_handle_t channel, uint32_t flags) {
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    zx_status_t status = zx_channel_read(
        channel, flags, bytes_.data() + bytes_reserved_, handles_.data(),
        bytes_.capacity(), handles_.capacity(), &actual_bytes, &actual_handles);
    if (status == ZX_OK) {
      bytes_.set_actual(actual_bytes);
      handles_.set_actual(actual_handles);
    }
    return status;
  }

  zx_status_t Write(zx_handle_t channel, uint32_t flags) {
    zx_status_t status =
        zx_channel_write(channel, flags, bytes_.data() + bytes_reserved_,
                         bytes_.actual(), handles_.data(), handles_.actual());
    return status;
  }

  template <typename T>
  T* AllocHeader() {
    FXL_CHECK(sizeof(T) <= bytes_reserved_);

    bytes_reserved_ -= sizeof(T);

    size_t actual = bytes_.actual() + sizeof(T);
    bytes_.set_actual(actual);

    return reinterpret_cast<T*>(bytes_.data() + bytes_reserved_);
  }

  uint8_t* data() { return bytes_.data() + bytes_reserved_; }
  size_t actual() { return bytes_.actual(); }
  size_t capacity() { return bytes_.capacity(); }

  fidl::HandlePart& handles() { return handles_; }

 private:
  size_t bytes_reserved_;
  fidl::BytePart bytes_;
  fidl::HandlePart handles_;
};

class MessageBuffer {
 public:
  MessageBuffer(size_t capacity = PAGE_SIZE,
                size_t reserved = kDefaultReservedSize)
      : buf_ptr_(new uint8_t[capacity]),
        reserved_(reserved),
        capacity_(capacity) {
    FXL_CHECK(buf_ptr_.get());
  }

  Message CreateEmptyMessage() {
    return Message(buf_ptr_.get(), capacity_, reserved_, handles_,
                   kDefaultHandleCapacity);
  }

  zx_handle_t* handles() { return handles_; }

 private:
  fbl::unique_ptr<uint8_t> buf_ptr_;
  size_t reserved_;
  size_t capacity_;
  zx_handle_t handles_[kDefaultHandleCapacity];
};

class MessageHandler {
 public:
  virtual ~MessageHandler() = default;

  virtual zx_status_t OnMessage(Message message) = 0;
};

class MessageReader {
 public:
  MessageReader(MessageHandler* message_handler, zx::channel channel,
                size_t max_message_size,
                async_dispatcher_t* async = async_get_default_dispatcher())
      : message_handler_(message_handler),
        channel_(fbl::move(channel)),
        max_message_size_(max_message_size),
        async_(async) {}
  ~MessageReader() { Stop(); }
  zx_status_t Start() { return WaitOnChannel(); }

  void Stop() { wait_.Cancel(); }
  zx_handle_t channel() { return channel_.get(); }

  void set_error_handler(fit::closure error_handler) {
    error_handler_ = std::move(error_handler);
  }

 private:
  zx_status_t WaitOnChannel() {
    wait_.set_object(channel_.get());
    wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    return wait_.Begin(async_);
  }

  void NotifyError() {
    Stop();
    if (error_handler_) {
      error_handler_();
    }
  }

  void OnMessage(async_dispatcher_t* async, async::WaitBase* wait,
                 zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      NotifyError();
      return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
      MessageBuffer buffer(max_message_size_);

      for (uint64_t i = 0; i < signal->count; i++) {
        auto message = buffer.CreateEmptyMessage();
        status = message.Read(channel_.get(), 0);
        if (status == ZX_ERR_SHOULD_WAIT) {
          break;
        }
        if (status != ZX_OK) {
          NotifyError();
          return;
        }

        status = message_handler_->OnMessage(std::move(message));
        if (status != ZX_OK) {
          NotifyError();
          return;
        }
      }

      status = WaitOnChannel();
      if (status != ZX_OK) {
        NotifyError();
      }
      return;
    }

    FXL_CHECK(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    // Notice that we don't notify an error until we've drained all the
    // messages out of the channel.
    NotifyError();
  }

  MessageHandler* message_handler_;
  fit::closure error_handler_;
  zx::channel channel_;
  size_t max_message_size_;
  async::WaitMethod<MessageReader, &MessageReader::OnMessage> wait_{this};
  async_dispatcher_t* const async_;
};

}  // namespace ree_agent
