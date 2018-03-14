// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/tracee.h"

#include <lib/async/default.h>
#include <trace-engine/fields.h>
#include <trace-provider/provider.h>

#include "lib/fxl/logging.h"

namespace tracing {
namespace {

// Writes |len| bytes from |buffer| to |socket|. Returns
// TransferStatus::kComplete if the entire buffer has been
// successfully transferred. A return value of
// TransferStatus::kReceiverDead indicates that the peer was closed
// during the transfer.
Tracee::TransferStatus WriteBufferToSocket(const uint8_t* buffer,
                                           size_t len,
                                           const zx::socket& socket) {
  size_t offset = 0;
  while (offset < len) {
    zx_status_t status = ZX_OK;
    size_t actual = 0;
    if ((status = socket.write(0u, buffer + offset, len - offset, &actual)) <
        0) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        zx_signals_t pending = 0;
        status = socket.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                                 zx::time::infinite(), &pending);
        if (status < 0) {
          FXL_LOG(ERROR) << "Wait on socket failed: " << status;
          return Tracee::TransferStatus::kCorrupted;
        }

        if (pending & ZX_SOCKET_WRITABLE)
          continue;

        if (pending & ZX_SOCKET_PEER_CLOSED) {
          FXL_LOG(ERROR) << "Peer closed while writing to socket";
          return Tracee::TransferStatus::kReceiverDead;
        }
      }

      return Tracee::TransferStatus::kCorrupted;
    }
    offset += actual;
  }

  return Tracee::TransferStatus::kComplete;
}

}  // namespace

Tracee::Tracee(TraceProviderBundle* bundle)
    : bundle_(bundle), wait_(this), weak_ptr_factory_(this) {}

Tracee::~Tracee() {
  if (async_) {
    wait_.Cancel(async_);
    wait_.set_object(ZX_HANDLE_INVALID);
    async_ = nullptr;
  }
}

bool Tracee::operator==(TraceProviderBundle* bundle) const {
  return bundle_ == bundle;
}

bool Tracee::Start(size_t buffer_size,
                   f1dl::Array<f1dl::String> categories,
                   fxl::Closure started_callback,
                   fxl::Closure stopped_callback) {
  FXL_DCHECK(state_ == State::kReady);
  FXL_DCHECK(!buffer_vmo_);
  FXL_DCHECK(started_callback);
  FXL_DCHECK(stopped_callback);

  zx::vmo buffer_vmo;
  zx_status_t status = zx::vmo::create(buffer_size, 0u, &buffer_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to create trace buffer: status=" << status;
    return false;
  }

  zx::vmo buffer_vmo_for_provider;
  status = buffer_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP,
                                &buffer_vmo_for_provider);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to duplicate trace buffer for provider: status="
                   << status;
    return false;
  }

  zx::eventpair fence, fence_for_provider;
  status = zx::eventpair::create(0u, &fence, &fence_for_provider);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to create trace buffer fence: status="
                   << status;
    return false;
  }

  bundle_->provider->Start(std::move(buffer_vmo_for_provider),
                           std::move(fence_for_provider),
                           std::move(categories));

  buffer_vmo_ = std::move(buffer_vmo);
  buffer_vmo_size_ = buffer_size;
  fence_ = std::move(fence);
  started_callback_ = std::move(started_callback);
  stopped_callback_ = std::move(stopped_callback);
  wait_.set_object(fence_.get());
  wait_.set_trigger(TRACE_PROVIDER_SIGNAL_STARTED |
                    TRACE_PROVIDER_SIGNAL_BUFFER_OVERFLOW |
                    ZX_EPAIR_PEER_CLOSED);
  async_ = async_get_default();
  status = wait_.Begin(async_);
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
  TransitionToState(State::kStartPending);
  return true;
}

void Tracee::Stop() {
  if (state_ != State::kStarted)
    return;
  bundle_->provider->Stop();
  TransitionToState(State::kStopping);
}

void Tracee::TransitionToState(State new_state) {
  FXL_VLOG(2) << *bundle_ << ": Transitioning from " << state_ << " to "
              << new_state;
  state_ = new_state;
}

async_wait_result_t Tracee::OnHandleReady(async_t* async,
                                          zx_status_t status,
                                          const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_VLOG(2) << *bundle_ << ": error=" << status;
    FXL_DCHECK(status == ZX_ERR_CANCELED);
    FXL_DCHECK(state_ == State::kStartPending || state_ == State::kStarted ||
               state_ == State::kStopping);
    wait_.set_object(ZX_HANDLE_INVALID);
    async_ = nullptr;
    TransitionToState(State::kStopped);
    return ASYNC_WAIT_FINISHED;
  }

  zx_signals_t pending = signal->observed;
  FXL_VLOG(2) << *bundle_ << ": pending=0x" << std::hex << pending;
  FXL_DCHECK(pending &
             (TRACE_PROVIDER_SIGNAL_STARTED |
              TRACE_PROVIDER_SIGNAL_BUFFER_OVERFLOW | ZX_EPAIR_PEER_CLOSED));
  FXL_DCHECK(state_ == State::kStartPending || state_ == State::kStarted ||
             state_ == State::kStopping);

  // Handle this before BUFFER_OVERFLOW so that if they arrive together we'll
  // have first transitioned to state kStarted before processing
  // BUFFER_OVERFLOW.
  if (pending & TRACE_PROVIDER_SIGNAL_STARTED) {
    // Clear the signal before invoking the callback:
    // a) It remains set until we do so,
    // b) Clear it before the call back in case we get back to back
    //    notifications.
    zx_object_signal(wait_.object(), TRACE_PROVIDER_SIGNAL_STARTED, 0u);
    // The provider should only be signalling us when it has finished startup.
    if (state_ == State::kStartPending) {
      TransitionToState(State::kStarted);
      fxl::Closure started_callback = std::move(started_callback_);
      FXL_DCHECK(started_callback);
      started_callback();
    } else {
      FXL_LOG(WARNING) << *bundle_
                       << ": Received TRACE_PROVIDER_SIGNAL_STARTED in state "
                       << state_;
    }
  }

  if (pending & TRACE_PROVIDER_SIGNAL_BUFFER_OVERFLOW) {
    // The signal remains set until we clear it.
    zx_object_signal(wait_.object(), TRACE_PROVIDER_SIGNAL_BUFFER_OVERFLOW, 0u);
    if (state_ == State::kStarted || state_ == State::kStopping) {
      FXL_LOG(WARNING)
          << *bundle_
          << ": Records got dropped, probably due to buffer overflow";
      buffer_overflow_ = true;
    } else {
      FXL_LOG(WARNING)
          << *bundle_
          << ": Received TRACE_PROVIDER_SIGNAL_BUFFER_OVERFLOW in state "
          << state_;
    }
  }

  if (pending & ZX_EPAIR_PEER_CLOSED) {
    wait_.set_object(ZX_HANDLE_INVALID);
    async_ = nullptr;
    TransitionToState(State::kStopped);
    fxl::Closure stopped_callback = std::move(stopped_callback_);
    FXL_DCHECK(stopped_callback);
    stopped_callback();
    return ASYNC_WAIT_FINISHED;
  }

  return ASYNC_WAIT_AGAIN;
}

Tracee::TransferStatus Tracee::TransferRecords(const zx::socket& socket) const {
  FXL_DCHECK(socket);
  FXL_DCHECK(buffer_vmo_);

  Tracee::TransferStatus transfer_status = TransferStatus::kComplete;

  if ((transfer_status = WriteProviderInfoRecord(socket)) !=
      TransferStatus::kComplete) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to write provider info record to trace.";
    return transfer_status;
  }

  if (buffer_overflow_) {
    // If we can't write the provider event record, it's not the end of the
    // world.
    if (WriteProviderBufferOverflowEvent(socket) != TransferStatus::kComplete) {
      FXL_LOG(ERROR) << *bundle_
                     << ": Failed to write provider event (buffer overflow)"
                        " record to trace.";
    }
  }

  std::vector<uint8_t> buffer(buffer_vmo_size_);

  size_t actual = 0;
  if ((buffer_vmo_.read(buffer.data(), 0, buffer_vmo_size_, &actual) !=
       ZX_OK) ||
      (actual != buffer_vmo_size_)) {
    FXL_LOG(WARNING) << *bundle_ << ": Failed to read data from buffer_vmo: "
                     << "actual size=" << actual
                     << ", expected size=" << buffer_vmo_size_;
  }

  const uint64_t* start = reinterpret_cast<const uint64_t*>(buffer.data());
  const uint64_t* current = start;
  const uint64_t* end = start + trace::BytesToWords(buffer.size());

  while (current < end) {
    auto length = trace::RecordFields::RecordSize::Get<uint16_t>(*current);
    if (length == 0 || length > trace::RecordFields::kMaxRecordSizeBytes ||
        current + length >= end) {
      break;
    }
    current += length;
  }

  return WriteBufferToSocket(buffer.data(),
                             trace::WordsToBytes(current - start), socket);
}

Tracee::TransferStatus Tracee::WriteProviderInfoRecord(
    const zx::socket& socket) const {
  FXL_DCHECK(bundle_->label.size() <=
             trace::ProviderInfoMetadataRecordFields::kMaxNameLength);

  size_t num_words =
      1u + trace::BytesToWords(trace::Pad(bundle_->label.size()));
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::ProviderInfoMetadataRecordFields::Type::Make(
          trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
      trace::ProviderInfoMetadataRecordFields::RecordSize::Make(num_words) |
      trace::ProviderInfoMetadataRecordFields::MetadataType::Make(
          trace::ToUnderlyingType(trace::MetadataType::kProviderInfo)) |
      trace::ProviderInfoMetadataRecordFields::Id::Make(bundle_->id) |
      trace::ProviderInfoMetadataRecordFields::NameLength::Make(
          bundle_->label.size());
  memcpy(&record[1], bundle_->label.c_str(), bundle_->label.size());
  return WriteBufferToSocket(reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words), socket);
}

Tracee::TransferStatus Tracee::WriteProviderBufferOverflowEvent(
    const zx::socket& socket) const {
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::ProviderEventMetadataRecordFields::Type::Make(
          trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
      trace::ProviderEventMetadataRecordFields::RecordSize::Make(num_words) |
      trace::ProviderEventMetadataRecordFields::MetadataType::Make(
          trace::ToUnderlyingType(trace::MetadataType::kProviderEvent)) |
      trace::ProviderEventMetadataRecordFields::Id::Make(bundle_->id) |
      trace::ProviderEventMetadataRecordFields::Event::Make(
          trace::ToUnderlyingType(trace::ProviderEventType::kBufferOverflow));
  return WriteBufferToSocket(reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words), socket);
}

std::ostream& operator<<(std::ostream& out, Tracee::State state) {
  switch (state) {
    case Tracee::State::kReady:
      out << "ready";
      break;
    case Tracee::State::kStartPending:
      out << "start pending";
      break;
    case Tracee::State::kStarted:
      out << "started";
      break;
    case Tracee::State::kStopping:
      out << "stopping";
      break;
    case Tracee::State::kStopped:
      out << "stopped";
      break;
  }

  return out;
}

}  // namespace tracing
