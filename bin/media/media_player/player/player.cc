// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/player/player.h"

#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/util/callback_joiner.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace {

static const Problem kMediaTypeNotSupported{kProblemMediaTypeNotSupported, ""};

}  // namespace

Player::Player(async_t* async) : graph_(async), async_(async) {}

Player::~Player() {}

void Player::SetSourceSegment(std::unique_ptr<SourceSegment> source_segment,
                              fxl::Closure callback) {
  if (source_segment_) {
    source_segment_->Flush(false);
    source_segment_->Deprovision();

    while (!streams_.empty()) {
      OnStreamRemoval(streams_.size() - 1);
    }

    for (auto& stream : streams_) {
      if (stream.sink_segment_) {
        if (stream.sink_segment_->connected()) {
          stream.sink_segment_->Unprepare();
          stream.sink_segment_->Disconnect();
        }
      }
    }
  }

  source_segment_ = std::move(source_segment);
  if (!source_segment_) {
    if (callback) {
      callback();
    }

    return;
  }

  set_source_segment_callback_ = callback;
  set_source_segment_countdown_ = 1;

  source_segment_->Provision(&graph_, async_,
                             [this]() {
                               // This callback notifies the player of changes
                               // to source_segment_'s problem() and/or
                               // metadata() values.
                               NotifyUpdate();
                             },
                             [this](size_t index, const StreamType* type,
                                    OutputRef output, bool more) {
                               if (output) {
                                 FXL_DCHECK(type);
                                 ++set_source_segment_countdown_;
                                 OnStreamUpdated(index, *type, output);
                               } else {
                                 FXL_DCHECK(!type);
                                 OnStreamRemoval(index);
                               }

                               if (!more) {
                                 MaybeCompleteSetSourceSegment();
                               }
                             });
}

void Player::SetSinkSegment(std::unique_ptr<SinkSegment> sink_segment,
                            StreamType::Medium medium) {
  // If we already have a sink segment for this medium, discard it.
  auto old_sink_segment = TakeSinkSegment(medium);
  if (old_sink_segment) {
    old_sink_segment->Deprovision();
    old_sink_segment.reset();
  }

  if (!sink_segment) {
    return;
  }

  sink_segment->Provision(&graph_, async_, [this]() {
    // This callback notifies the player of changes to source_segment_'s
    // problem() and/or end_of_stream() values.
    NotifyUpdate();
  });

  Stream* stream = GetStream(medium);
  if (stream) {
    FXL_DCHECK(!stream->sink_segment_);
    stream->sink_segment_ = std::move(sink_segment);
    ConnectAndPrepareStream(stream);
    return;
  }

  // We have no stream for this medium. Park the segment.
  parked_sink_segments_[medium] = std::move(sink_segment);
}

void Player::Prime(fxl::Closure callback) {
  auto callback_joiner = CallbackJoiner::Create();

  for (auto& stream : streams_) {
    if (stream.sink_segment_) {
      stream.sink_segment_->Prime(callback_joiner->NewCallback());
    }
  }

  callback_joiner->WhenJoined(callback);
}

void Player::Flush(bool hold_frame) {
  if (source_segment_) {
    source_segment_->Flush(hold_frame);
  }
}

void Player::SetTimelineFunction(media::TimelineFunction timeline_function,
                                 fxl::Closure callback) {
  FXL_DCHECK(timeline_function.reference_delta() != 0);

  int64_t reference_time = timeline_function.reference_time();
  if (reference_time == media::kUnspecifiedTime) {
    reference_time = media::Timeline::local_now() + kMinimumLeadTime;
  }

  int64_t subject_time = timeline_function.subject_time();
  if (subject_time == media::kUnspecifiedTime) {
    subject_time = timeline_function_(reference_time);
  }

  timeline_function_ = media::TimelineFunction(subject_time, reference_time,
                                               timeline_function.rate());

  auto callback_joiner = CallbackJoiner::Create();

  for (auto& stream : streams_) {
    if (stream.sink_segment_) {
      stream.sink_segment_->SetTimelineFunction(timeline_function_,
                                                callback_joiner->NewCallback());
    }
  }

  callback_joiner->WhenJoined(callback);
}

void Player::SetProgramRange(uint64_t program,
                             int64_t min_pts,
                             int64_t max_pts) {
  for (auto& stream : streams_) {
    if (stream.sink_segment_) {
      stream.sink_segment_->SetProgramRange(program, min_pts, max_pts);
    }
  }
}

void Player::Seek(int64_t position, fxl::Closure callback) {
  if (source_segment_) {
    source_segment_->Seek(position, callback);
  } else {
    callback();
  }
}

bool Player::end_of_stream() const {
  bool result = false;

  for (auto& stream : streams_) {
    if (stream.sink_segment_) {
      if (!stream.sink_segment_->end_of_stream()) {
        return false;
      }

      result = true;
    }
  }

  return result;
}

const Metadata* Player::metadata() const {
  if (source_segment_ && source_segment_->metadata()) {
    return source_segment_->metadata();
  }

  return nullptr;
}

const Problem* Player::problem() const {
  // First, see if the source segment has a problem to report.
  if (source_segment_ && source_segment_->problem()) {
    return source_segment_->problem();
  }

  // If there's video and a video sink segment, but we couldn't connect it,
  // report that as a problem.
  if (content_has_medium(StreamType::Medium::kVideo) &&
      has_sink_segment(StreamType::Medium::kVideo) &&
      !medium_connected(StreamType::Medium::kVideo)) {
    return &kMediaTypeNotSupported;
  }

  // If there's audio and a audio sink segment, but we couldn't connect it,
  // report that as a problem.
  if (content_has_medium(StreamType::Medium::kAudio) &&
      has_sink_segment(StreamType::Medium::kAudio) &&
      !medium_connected(StreamType::Medium::kAudio)) {
    return &kMediaTypeNotSupported;
  }

  // See if any of the sink segments have a problem to report.
  for (auto& stream : streams_) {
    if (stream.sink_segment_ && stream.sink_segment_->problem()) {
      return stream.sink_segment_->problem();
    }
  }

  return nullptr;
}

void Player::NotifyUpdate() {
  if (update_callback_) {
    update_callback_();
  }
}

const Player::Stream* Player::GetStream(StreamType::Medium medium) const {
  for (auto& stream : streams_) {
    if (stream.stream_type_ && stream.stream_type_->medium() == medium) {
      return &stream;
    }
  }

  return nullptr;
}

Player::Stream* Player::GetStream(StreamType::Medium medium) {
  for (auto& stream : streams_) {
    if (stream.stream_type_ && stream.stream_type_->medium() == medium) {
      return &stream;
    }
  }

  return nullptr;
}

SinkSegment* Player::GetParkedSinkSegment(StreamType::Medium medium) const {
  auto iter = parked_sink_segments_.find(medium);
  return iter == parked_sink_segments_.end() ? nullptr : iter->second.get();
}

void Player::OnStreamUpdated(size_t index,
                             const StreamType& type,
                             OutputRef output) {
  if (streams_.size() < index + 1) {
    streams_.resize(index + 1);
  }

  Stream& stream = streams_[index];

  if (stream.sink_segment_) {
    FXL_DCHECK(stream.stream_type_);

    if (stream.stream_type_->medium() != type.medium()) {
      // The sink segment for this stream is for the wrong medium. Park it.
      FXL_DCHECK(!GetParkedSinkSegment(stream.stream_type_->medium()));
      parked_sink_segments_[stream.stream_type_->medium()] =
          TakeSinkSegment(&stream);
    }
  }

  stream.stream_type_ = type.Clone();
  stream.output_ = output;

  if (!stream.sink_segment_) {
    stream.sink_segment_ = TakeSinkSegment(stream.stream_type_->medium());
    if (!stream.sink_segment_) {
      // No sink segment has been registered for this medium.
      MaybeCompleteSetSourceSegment();
      return;
    }
  }

  ConnectAndPrepareStream(&stream);
}

void Player::OnStreamRemoval(size_t index) {
  if (streams_.size() < index + 1) {
    return;
  }

  Stream& stream = streams_[index];

  if (stream.sink_segment_) {
    FXL_DCHECK(stream.stream_type_);

    // Park this stream segment.
    FXL_DCHECK(!GetParkedSinkSegment(stream.stream_type_->medium()));
    parked_sink_segments_[stream.stream_type_->medium()] =
        TakeSinkSegment(&stream);
  }

  stream.stream_type_ = nullptr;
  stream.output_ = nullptr;

  // Remove unused entries at the back of streams_.
  while (!streams_.empty() && !streams_[streams_.size() - 1].stream_type_) {
    streams_.resize(streams_.size() - 1);
  }
}

void Player::MaybeCompleteSetSourceSegment() {
  if (!set_source_segment_callback_) {
    return;
  }

  if (--set_source_segment_countdown_ == 0) {
    fxl::Closure callback = set_source_segment_callback_;
    set_source_segment_callback_ = nullptr;
    callback();
  }
}

std::unique_ptr<SinkSegment> Player::TakeSinkSegment(
    StreamType::Medium medium) {
  auto iter = parked_sink_segments_.find(medium);

  if (iter != parked_sink_segments_.end()) {
    auto result = std::move(iter->second);
    parked_sink_segments_.erase(iter);
    return result;
  }

  Stream* stream = GetStream(medium);
  if (stream && stream->sink_segment_) {
    return TakeSinkSegment(stream);
  }

  return nullptr;
}

std::unique_ptr<SinkSegment> Player::TakeSinkSegment(Stream* stream) {
  FXL_DCHECK(stream);
  FXL_DCHECK(stream->sink_segment_);

  if (stream->sink_segment_->connected()) {
    stream->sink_segment_->Unprepare();
    stream->sink_segment_->Disconnect();
  }

  return std::move(stream->sink_segment_);
}

void Player::ConnectAndPrepareStream(Stream* stream) {
  FXL_DCHECK(stream);
  FXL_DCHECK(stream->sink_segment_);
  FXL_DCHECK(stream->stream_type_);
  FXL_DCHECK(stream->output_);

  stream->sink_segment_->Connect(
      *stream->stream_type_, stream->output_,
      [this, medium = stream->stream_type_->medium()]() {
        Stream* stream = GetStream(medium);
        if (stream && stream->sink_segment_) {
          stream->sink_segment_->Prepare();
        }

        MaybeCompleteSetSourceSegment();
      });
}

void Player::Dump(std::ostream& os) const {
  source_node().GetGenericNode()->Dump(os, source_node());
}

}  // namespace media_player
