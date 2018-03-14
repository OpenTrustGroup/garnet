// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tracer.h"

#include <utility>

#include <fbl/type_support.h>
#include <lib/async/default.h>
#include <trace-engine/fields.h>
#include <trace-reader/reader.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

using namespace tracing::internal;

namespace tracing {
namespace {

// Note: Buffer needs to be big enough to store records of maximum size.
constexpr size_t kReadBufferSize = trace::RecordFields::kMaxRecordSizeBytes * 4;

}  // namespace

Tracer::Tracer(TraceController* controller)
    : controller_(controller), async_(nullptr), wait_(this) {
  FXL_DCHECK(controller_);
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
}

Tracer::~Tracer() {
  CloseSocket();
}

void Tracer::Start(TraceOptionsPtr options,
                   RecordConsumer record_consumer,
                   ErrorHandler error_handler,
                   fxl::Closure start_callback,
                   fxl::Closure done_callback) {
  FXL_DCHECK(state_ == State::kStopped);

  state_ = State::kStarted;
  done_callback_ = std::move(done_callback);
  start_callback_ = std::move(start_callback);

  zx::socket outgoing_socket;
  zx_status_t status = zx::socket::create(0u, &socket_, &outgoing_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: status=" << status;
    Done();
    return;
  }

  controller_->StartTracing(std::move(options), std::move(outgoing_socket),
                            [this]() { start_callback_(); });

  buffer_.reserve(kReadBufferSize);
  reader_.reset(new trace::TraceReader(fbl::move(record_consumer),
                                       fbl::move(error_handler)));

  async_ = async_get_default();
  wait_.set_object(socket_.get());
  status = wait_.Begin(async_);
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
}

void Tracer::Stop() {
  // Note: The controller will close the socket when finished.
  if (state_ == State::kStarted) {
    state_ = State::kStopping;
    controller_->StopTracing();
  }
}

async_wait_result_t Tracer::OnHandleReady(async_t* async,
                                          zx_status_t status,
                                          const zx_packet_signal_t* signal) {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  if (signal->observed & ZX_SOCKET_READABLE) {
    return DrainSocket();
  } else if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    Done();
    return ASYNC_WAIT_FINISHED;
  } else {
    FXL_CHECK(false);
    return ASYNC_WAIT_FINISHED;
  }
}

async_wait_result_t Tracer::DrainSocket() {
  for (;;) {
    size_t actual;
    zx_status_t status =
        socket_.read(0u, buffer_.data() + buffer_end_,
                     buffer_.capacity() - buffer_end_, &actual);
    if (status == ZX_ERR_SHOULD_WAIT)
      return ASYNC_WAIT_AGAIN;

    if (status || actual == 0) {
      if (status != ZX_ERR_PEER_CLOSED) {
        FXL_LOG(ERROR) << "Failed to read data from socket: status=" << status;
      }
      Done();
      return ASYNC_WAIT_FINISHED;
    }

    buffer_end_ += actual;
    size_t bytes_available = buffer_end_;
    FXL_DCHECK(bytes_available > 0);

    trace::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                       trace::BytesToWords(bytes_available));
    if (!reader_->ReadRecords(chunk)) {
      FXL_LOG(ERROR) << "Trace stream is corrupted";
      Done();
      return ASYNC_WAIT_FINISHED;
    }

    size_t bytes_consumed =
        bytes_available - trace::WordsToBytes(chunk.remaining_words());
    bytes_available -= bytes_consumed;
    memmove(buffer_.data(), buffer_.data() + bytes_consumed, bytes_available);
    buffer_end_ = bytes_available;
  }
}

void Tracer::CloseSocket() {
  if (socket_) {
    wait_.Cancel(async_);
    wait_.set_object(ZX_HANDLE_INVALID);
    async_ = nullptr;
    socket_.reset();
  }
}

void Tracer::Done() {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  state_ = State::kStopped;
  reader_.reset();

  CloseSocket();

  if (done_callback_) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(done_callback_));
  }
}

}  // namespace tracing
