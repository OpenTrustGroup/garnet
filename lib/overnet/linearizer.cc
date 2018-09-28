// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linearizer.h"

namespace overnet {

Linearizer::Linearizer(uint64_t max_buffer, TraceSink trace_sink)
    : max_buffer_(max_buffer),
      trace_sink_(trace_sink.Decorate(
          [](const std::string& msg) { return "Linearizer " + msg; })) {}

Linearizer::~Linearizer() {
  switch (read_mode_) {
    case ReadMode::Idle:
      break;
    case ReadMode::Closed:
      read_data_.closed.~Closed();
      break;
    case ReadMode::ReadAll:
      read_data_.read_all.~ReadAll();
      break;
    case ReadMode::ReadSlice:
      read_data_.read_slice.~ReadSlice();
      break;
  }
}

void Linearizer::ValidateInternals() const {
#ifndef NDEBUG
  // If closed, nothing should be pending
  if (read_mode_ == ReadMode::Closed) {
    assert(pending_push_.empty());
  }
  // No pending read callback if the next thing is ready.
  if ((!pending_push_.empty() && pending_push_.begin()->first == offset_)) {
    assert(read_mode_ == ReadMode::Idle);
  }
  // The first thing in the pending queue should be after our read bytes.
  if (!pending_push_.empty())
    assert(pending_push_.begin()->first >= offset_);
  // There should be no overlap between chunks in the pending map.
  uint64_t seen_to = offset_;
  for (const auto& el : pending_push_) {
    assert(seen_to <= el.first);
    seen_to = el.first + el.second.length();
  }
  // Should not exceed our buffering limits.
  if (!pending_push_.empty()) {
    auto last = std::prev(pending_push_.end());
    assert(last->first + last->second.length() <= offset_ + max_buffer_);
  }
#endif
}

void Linearizer::Close(const Status& status) {
  Close(status, Callback<void>::Ignored());
}

void Linearizer::Close(const Status& status, Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Close " << status << " mode=" << read_mode_;
  if (status.is_ok() && !pending_push_.empty()) {
    Close(Status(StatusCode::CANCELLED, "Gaps existed at close time"));
    return;
  }
  switch (read_mode_) {
    case ReadMode::Closed:
      break;
    case ReadMode::Idle:
      IdleToClosed(status);
      pending_push_.clear();
      break;
    case ReadMode::ReadSlice: {
      auto push = std::move(ReadSliceToIdle().done);
      IdleToClosed(status);
      pending_push_.clear();
      if (status.is_ok()) {
        push(Nothing);
      } else {
        push(status);
      }
    } break;
    case ReadMode::ReadAll: {
      auto rd = ReadAllToIdle();
      IdleToClosed(status);
      pending_push_.clear();
      if (status.is_ok()) {
        rd.done(std::move(rd.building));
      } else {
        rd.done(status);
      }
    } break;
  }
}

void Linearizer::Push(Chunk chunk) {
  ValidateInternals();

  uint64_t chunk_start = chunk.offset;
  const uint64_t chunk_end = chunk_start + chunk.slice.length();

  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Push start=" << chunk_start << " end=" << chunk_end
      << " end-of-message=" << chunk.end_of_message
      << " lin-offset=" << offset_;

  // Check whether the chunk is within our buffering limits
  // (if not we can reject and hope for a resend.)
  if (chunk_end > offset_ + max_buffer_) {
    OVERNET_TRACE(DEBUG, trace_sink_)
        << "Push reject: past end of buffering window;"
        << " max_buffer=" << max_buffer_;
    return;
  }

  if (length_) {
    if (chunk_end > *length_) {
      Close(Status(StatusCode::INVALID_ARGUMENT,
                   "Received chunk past end of message"));
    } else if (chunk.end_of_message && *length_ != chunk_end) {
      Close(Status(StatusCode::INVALID_ARGUMENT,
                   "Received ambiguous end of message point"));
    }
  } else if (chunk.end_of_message) {
    if (offset_ > chunk_end) {
      Close(Status(StatusCode::INVALID_ARGUMENT,
                   "Already read past end of message"));
    }
    if (!pending_push_.empty()) {
      const auto it = pending_push_.rbegin();
      const auto end = it->first + it->second.length();
      if (end > chunk_end) {
        Close(Status(StatusCode::INVALID_ARGUMENT,
                     "Already received bytes past end of message"));
      }
    }
    if (offset_ == chunk_end) {
      Close(Status::Ok());
    }
  }

  if (read_mode_ == ReadMode::Closed) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "Push reject: closed";
    return;
  }

  if (chunk.end_of_message && !length_) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "Push: record length";
    length_ = chunk_end;
  }

  // Fast path: already a pending read ready, this chunk is at the head of what
  // we're waiting for, and overlaps with nothing.
  if (read_mode_ == ReadMode::ReadSlice && chunk_start == offset_ &&
      (pending_push_.empty() || pending_push_.begin()->first > chunk_end)) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "Push: fast-path";
    offset_ += chunk.slice.length();
    auto push = std::move(ReadSliceToIdle().done);
    if (length_) {
      assert(offset_ <= *length_);
      if (offset_ == *length_) {
        Close(Status::Ok());
      }
    }
    push(std::move(chunk.slice));
    return;
  }

  // If the chunk is partially before the start of what we've delivered, we can
  // trim.
  // If it's wholly before, then we can discard.
  if (chunk_start < offset_) {
    if (chunk_end > offset_) {
      OVERNET_TRACE(DEBUG, trace_sink_) << "Push: trim begin";
      chunk.TrimBegin(offset_ - chunk_start);
      chunk_start = chunk.offset;
    } else {
      OVERNET_TRACE(DEBUG, trace_sink_) << "Push: all prior";
      return;
    }
  }

  // Slow path: we first integrate this chunk into pending_push_, and then see
  // if we can trigger any completions.
  // We break out the integration into a separate function since it has many
  // exit conditions, and we've got some common checks to do once it's finished.
  if (pending_push_.empty()) {
    OVERNET_TRACE(DEBUG, trace_sink_) << "Push: first pending";
    pending_push_.emplace(chunk.offset, std::move(chunk.slice));
  } else {
    IntegratePush(std::move(chunk));
  }

  switch (read_mode_) {
    case ReadMode::Idle:
    case ReadMode::Closed:
      break;
    case ReadMode::ReadSlice:
      Pull(std::move(ReadSliceToIdle().done));
      break;
    case ReadMode::ReadAll:
      ContinueReadAll();
      break;
  }
}

void Linearizer::IntegratePush(Chunk chunk) {
  assert(!pending_push_.empty());

  auto trace_sink = trace_sink_.Decorate(
      [start = chunk.offset,
       end = chunk.offset + chunk.slice.length()](const std::string& msg) {
        std::ostringstream out;
        out << "IntegratePush: start=" << start << " end=" << end << " " << msg;
        return out.str();
      });

  auto lb = pending_push_.lower_bound(chunk.offset);
  if (lb != pending_push_.end() && lb->first == chunk.offset) {
    // Coincident with another chunk we've already received.
    // First check whether the common bytes are the same.
    const size_t common_length =
        std::min(chunk.slice.length(), lb->second.length());
    OVERNET_TRACE(DEBUG, trace_sink)
        << "coincident with existing; common_length=" << common_length;
    if (0 != memcmp(chunk.slice.begin(), lb->second.begin(), common_length)) {
      Close(Status(StatusCode::DATA_LOSS,
                   "Linearizer received different bytes for the same span"));
    } else if (chunk.slice.length() <= lb->second.length()) {
      // New chunk is shorter than what's there (or the same length): We're
      // done.
    } else {
      // New chunk is bigger than what's there: we create a new (tail) chunk and
      // continue integration
      chunk.TrimBegin(lb->second.length());
      IntegratePush(std::move(chunk));
    }
    // Early out.
    return;
  }

  if (lb != pending_push_.begin()) {
    // Find the chunk *before* this one
    const auto before = std::prev(lb);
    assert(before->first < chunk.offset);
    // Check to see if that chunk overlaps with this one.
    const size_t before_end = before->first + before->second.length();
    OVERNET_TRACE(DEBUG, trace_sink)
        << "prior chunk start=" << before->first << " end=" << before_end;
    if (before_end > chunk.offset) {
      // Prior chunk overlaps with this one.
      // First check whether the common bytes are the same.
      const size_t common_length =
          std::min(before_end - chunk.offset, uint64_t(chunk.slice.length()));
      OVERNET_TRACE(DEBUG, trace_sink)
          << "overlap with prior; common_length=" << common_length;
      if (0 != memcmp(before->second.begin() + (chunk.offset - before->first),
                      chunk.slice.begin(), common_length)) {
        Close(Status(StatusCode::DATA_LOSS,
                     "Linearizer received different bytes for the same span"));
      } else if (before_end >= chunk.offset + chunk.slice.length()) {
        // New chunk is a subset of the one before: we're done.
      } else {
        // Trim the new chunk and continue integration.
        chunk.TrimBegin(before_end - chunk.offset);
        IntegratePush(std::move(chunk));
      }
      // Early out.
      return;
    }
  }

  if (lb != pending_push_.end()) {
    // Find the chunk *after* this one.
    const auto after = lb;
    assert(after->first > chunk.offset);
    // Check to see if that chunk overlaps with this one.
    OVERNET_TRACE(DEBUG, trace_sink)
        << "subsequent chunk start=" << after->first
        << " end=" << (after->first + after->second.length());
    if (after->first < chunk.offset + chunk.slice.length()) {
      const size_t common_length =
          std::min(chunk.offset + chunk.slice.length() - after->first,
                   uint64_t(after->second.length()));
      OVERNET_TRACE(DEBUG, trace_sink)
          << "overlap with subsequent; common_length=" << common_length;
      if (0 != memcmp(after->second.begin(),
                      chunk.slice.begin() + (after->first - chunk.offset),
                      common_length)) {
        Close(Status(StatusCode::DATA_LOSS,
                     "Linearizer received different bytes for the same span"));
        return;
      } else if (after->first + after->second.length() <
                 chunk.offset + chunk.slice.length()) {
        OVERNET_TRACE(DEBUG, trace_sink) << "Split and integrate separately";
        // Split chunk into two and integrate each separately
        Chunk tail = chunk;
        chunk.TrimEnd(chunk.offset + chunk.slice.length() - after->first);
        tail.TrimBegin(after->first + after->second.length() - tail.offset);
        IntegratePush(std::move(chunk));
        IntegratePush(std::move(tail));
        return;
      } else {
        // Trim so the new chunk no longer overlaps.
        chunk.TrimEnd(chunk.offset + chunk.slice.length() - after->first);
      }
    }
  }

  // We now have a non-overlapping chunk that we can insert.
  OVERNET_TRACE(DEBUG, trace_sink)
      << "add pending start=" << chunk.offset
      << " end=" << (chunk.offset + chunk.slice.length());
  pending_push_.emplace_hint(lb, chunk.offset, std::move(chunk.slice));
}

void Linearizer::Pull(StatusOrCallback<Optional<Slice>> push) {
  ValidateInternals();
  switch (read_mode_) {
    case ReadMode::Closed:
      if (read_data_.closed.status.is_ok()) {
        push(Nothing);
      } else {
        push(read_data_.closed.status);
      }
      break;
    case ReadMode::ReadSlice:
    case ReadMode::ReadAll:
      abort();
    case ReadMode::Idle: {
      // Check to see if there's data already available.
      auto it = pending_push_.begin();
      if (it != pending_push_.end() && it->first == offset_) {
        // There is!
        Slice slice = std::move(it->second);
        pending_push_.erase(it);
        offset_ += slice.length();
        if (length_) {
          assert(offset_ <= *length_);
          if (offset_ == *length_) {
            Close(Status::Ok());
          }
        }
        push(std::move(slice));
      } else {
        // There's not, signal that we can take some.
        // Note that this will cancel any pending Pull().
        IdleToReadSlice(std::move(push));
      }
    } break;
  }
  ValidateInternals();
}

void Linearizer::PullAll(StatusOrCallback<std::vector<Slice>> push) {
  ValidateInternals();
  switch (read_mode_) {
    case ReadMode::Closed:
      if (read_data_.closed.status.is_ok()) {
        push(std::vector<Slice>());
      } else {
        push(read_data_.closed.status);
      }
      break;
    case ReadMode::ReadSlice:
    case ReadMode::ReadAll:
      abort();
    case ReadMode::Idle:
      IdleToReadAll(std::move(push));
      ContinueReadAll();
  }
  ValidateInternals();
}

void Linearizer::ContinueReadAll() {
  for (;;) {
    assert(read_mode_ == ReadMode::ReadAll);
    auto it = pending_push_.begin();
    if (it == pending_push_.end()) {
      return;
    }
    if (it->first != offset_) {
      return;
    }
    auto slice = std::move(it->second);
    pending_push_.erase(it);
    offset_ += slice.length();
    read_data_.read_all.building.emplace_back(std::move(slice));
    if (length_) {
      assert(offset_ <= *length_);
      if (offset_ == *length_) {
        Close(Status::Ok());
        return;
      }
    }
  }
}

void Linearizer::IdleToClosed(const Status& status) {
  assert(read_mode_ == ReadMode::Idle);
  read_mode_ = ReadMode::Closed;
  new (&read_data_.closed) Closed{status};
}

void Linearizer::IdleToReadSlice(StatusOrCallback<Optional<Slice>> done) {
  assert(read_mode_ == ReadMode::Idle);
  read_mode_ = ReadMode::ReadSlice;
  new (&read_data_.read_slice) ReadSlice{std::move(done)};
}

void Linearizer::IdleToReadAll(StatusOrCallback<std::vector<Slice>> done) {
  assert(read_mode_ == ReadMode::Idle);
  read_mode_ = ReadMode::ReadAll;
  new (&read_data_.read_all) ReadAll{{}, std::move(done)};
}

Linearizer::ReadSlice Linearizer::ReadSliceToIdle() {
  assert(read_mode_ == ReadMode::ReadSlice);
  ReadSlice tmp = std::move(read_data_.read_slice);
  read_data_.read_slice.~ReadSlice();
  read_mode_ = ReadMode::Idle;
  return tmp;
}

Linearizer::ReadAll Linearizer::ReadAllToIdle() {
  assert(read_mode_ == ReadMode::ReadAll);
  ReadAll tmp = std::move(read_data_.read_all);
  read_data_.read_all.~ReadAll();
  read_mode_ = ReadMode::Idle;
  return tmp;
}

}  // namespace overnet
