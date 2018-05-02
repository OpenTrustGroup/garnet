// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/fake_renderer.h"

#include <iomanip>
#include <iostream>
#include <limits>

#include "lib/media/timeline/timeline.h"

namespace media_player {

namespace {

static uint64_t Hash(const void* data, size_t data_size) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  uint64_t hash = 0;

  for (; data_size != 0; --data_size, ++bytes) {
    hash = *bytes + (hash << 6) + (hash << 16) - hash;
  }

  return hash;
}

}  // namespace

FakeRenderer::FakeRenderer()
    : renderer_binding_(this),
      control_point_binding_(this),
      timeline_consumer_binding_(this) {}

FakeRenderer::~FakeRenderer() {
  SendStatusUpdates();
  ClearPendingTimelineFunction(false);
}

void FakeRenderer::Bind(
    fidl::InterfaceRequest<MediaRenderer> renderer_request) {
  renderer_binding_.Bind(std::move(renderer_request));
}

void FakeRenderer::GetSupportedMediaTypes(
    GetSupportedMediaTypesCallback callback) {
  fidl::VectorPtr<media::MediaTypeSet> supported_types;

  {
    media::AudioMediaTypeSetDetails audio_details;
    audio_details.sample_format = media::AudioSampleFormat::ANY;
    audio_details.min_channels = 1;
    audio_details.max_channels = std::numeric_limits<uint32_t>::max();
    audio_details.min_frames_per_second = 1;
    audio_details.max_frames_per_second = std::numeric_limits<uint32_t>::max();
    media::MediaTypeSet supported_type;
    supported_type.medium = media::MediaTypeMedium::AUDIO;
    supported_type.details.set_audio(std::move(audio_details));
    supported_type.encodings.push_back(media::kAudioEncodingLpcm);
    supported_types.push_back(std::move(supported_type));
  }

  {
    media::VideoMediaTypeSetDetails video_details;
    video_details.min_width = 1;
    video_details.max_width = std::numeric_limits<uint32_t>::max();
    video_details.min_height = 1;
    video_details.max_height = std::numeric_limits<uint32_t>::max();
    media::MediaTypeSet supported_type;
    supported_type.medium = media::MediaTypeMedium::VIDEO;
    supported_type.details.set_video(std::move(video_details));
    supported_type.encodings.push_back(media::kVideoEncodingUncompressed);
    supported_types.push_back(std::move(supported_type));
  }

  callback(std::move(supported_types));
}

void FakeRenderer::SetMediaType(media::MediaType media_type) {
  if (media_type.details.is_video()) {
    pts_rate_ = media::TimelineRate::NsPerSecond;
  } else if (media_type.details.is_audio()) {
    const media::AudioMediaTypeDetails& details = media_type.details.audio();
    pts_rate_ = media::TimelineRate(details.frames_per_second, 1);
  } else {
    FXL_DCHECK(false) << "Media type is neither audio nor video";
  }

  SetPtsRate(pts_rate_);
}

void FakeRenderer::GetPacketConsumer(
    fidl::InterfaceRequest<media::MediaPacketConsumer>
        packet_consumer_request) {
  MediaPacketConsumerBase::Bind(std::move(packet_consumer_request));
}

void FakeRenderer::GetTimelineControlPoint(
    fidl::InterfaceRequest<media::MediaTimelineControlPoint>
        control_point_request) {
  control_point_binding_.Bind(std::move(control_point_request));
}

void FakeRenderer::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  FXL_DCHECK(supplied_packet);
  FXL_DCHECK(supplied_packet->packet().pts_rate_ticks ==
             pts_rate_.subject_delta());
  FXL_DCHECK(supplied_packet->packet().pts_rate_seconds ==
             pts_rate_.reference_delta());
  if (supplied_packet->packet().flags & media::kFlagEos) {
    end_of_stream_ = true;
    SendStatusUpdates();
  }

  if (dump_packets_) {
    std::cerr << "{ " << supplied_packet->packet().pts << ", "
              << ((supplied_packet->packet().flags & media::kFlagEos) ? "true"
                                                                      : "false")
              << ", " << supplied_packet->payload_size() << ", 0x" << std::hex
              << std::setw(16) << std::setfill('0')
              << Hash(supplied_packet->payload(),
                      supplied_packet->payload_size())
              << std::dec << " },\n";
  }

  if (!expected_packets_info_.empty()) {
    if (expected_packets_info_iter_ == expected_packets_info_.end()) {
      FXL_DLOG(ERROR) << "packet supplied after expected packets";
      expected_ = false;
    }

    if (expected_packets_info_iter_->pts() != supplied_packet->packet().pts ||
        expected_packets_info_iter_->end_of_stream() !=
            (supplied_packet->packet().flags & media::kFlagEos) ||
        expected_packets_info_iter_->size() !=
            supplied_packet->payload_size() ||
        expected_packets_info_iter_->hash() !=
            Hash(supplied_packet->payload(), supplied_packet->payload_size())) {
      FXL_DLOG(ERROR) << "supplied packet doesn't match expected packet info";
      expected_ = false;
    }

    ++expected_packets_info_iter_;
  }

  packet_queue_.push(std::move(supplied_packet));
  while (packet_queue_.size() >= demand_min_packets_outstanding_) {
    packet_queue_.pop();
  }
}

void FakeRenderer::OnFlushRequested(bool hold_frame, FlushCallback callback) {
  while (!packet_queue_.empty()) {
    packet_queue_.pop();
  }
  callback();
}

void FakeRenderer::OnFailure() {
  if (renderer_binding_.is_bound()) {
    renderer_binding_.Unbind();
  }

  if (control_point_binding_.is_bound()) {
    control_point_binding_.Unbind();
  }

  if (timeline_consumer_binding_.is_bound()) {
    timeline_consumer_binding_.Unbind();
  }

  MediaPacketConsumerBase::OnFailure();
}

void FakeRenderer::GetStatus(uint64_t version_last_seen,
                             GetStatusCallback callback) {
  if (version_last_seen < status_version_) {
    CompleteGetStatus(callback);
  } else {
    pending_status_callbacks_.push_back(callback);
  }
}

void FakeRenderer::GetTimelineConsumer(
    fidl::InterfaceRequest<TimelineConsumer> timeline_consumer_request) {
  timeline_consumer_binding_.Bind(std::move(timeline_consumer_request));
}

void FakeRenderer::SetProgramRange(uint64_t program,
                                   int64_t min_pts,
                                   int64_t max_pts) {}

void FakeRenderer::Prime(PrimeCallback callback) {
  SetDemand(demand_min_packets_outstanding_);
  callback();
}

void FakeRenderer::SetTimelineTransform(
    media::TimelineTransform timeline_transform,
    SetTimelineTransformCallback callback) {
  SetTimelineTransformNoReply(std::move(timeline_transform));

  set_timeline_transform_callback_ = callback;
}

void FakeRenderer::SetTimelineTransformNoReply(
    media::TimelineTransform timeline_transform) {
  FXL_DCHECK(timeline_transform.reference_delta != 0);

  if (timeline_transform.subject_time != media::kUnspecifiedTime) {
    end_of_stream_ = false;
  }

  int64_t reference_time =
      timeline_transform.reference_time == media::kUnspecifiedTime
          ? media::Timeline::local_now()
          : timeline_transform.reference_time;
  int64_t subject_time =
      timeline_transform.subject_time == media::kUnspecifiedTime
          ? current_timeline_function_(reference_time)
          : timeline_transform.subject_time;

  // Eject any previous pending change.
  ClearPendingTimelineFunction(false);

  // Queue up the new pending change.
  pending_timeline_function_ = media::TimelineFunction(
      subject_time, reference_time, timeline_transform.subject_delta,
      timeline_transform.reference_delta);
}

void FakeRenderer::ClearPendingTimelineFunction(bool completed) {
  pending_timeline_function_ = media::TimelineFunction(
      media::kUnspecifiedTime, media::kUnspecifiedTime, 0, 1);
  if (set_timeline_transform_callback_) {
    set_timeline_transform_callback_(completed);
    set_timeline_transform_callback_ = nullptr;
  }
}

void FakeRenderer::MaybeApplyPendingTimelineChange(int64_t reference_time) {
  if (pending_timeline_function_.reference_time() == media::kUnspecifiedTime ||
      pending_timeline_function_.reference_time() > reference_time) {
    return;
  }

  current_timeline_function_ = pending_timeline_function_;
  pending_timeline_function_ = media::TimelineFunction(
      media::kUnspecifiedTime, media::kUnspecifiedTime, 0, 1);

  if (set_timeline_transform_callback_) {
    set_timeline_transform_callback_(true);
    set_timeline_transform_callback_ = nullptr;
  }

  SendStatusUpdates();
}

void FakeRenderer::SendStatusUpdates() {
  ++status_version_;

  std::vector<GetStatusCallback> pending_status_callbacks;
  pending_status_callbacks_.swap(pending_status_callbacks);

  for (GetStatusCallback pending_status_callback : pending_status_callbacks) {
    CompleteGetStatus(pending_status_callback);
  }
}

void FakeRenderer::CompleteGetStatus(GetStatusCallback callback) {
  media::MediaTimelineControlPointStatus status;
  status.timeline_transform = current_timeline_function_.ToTimelineTransform();
  status.end_of_stream = end_of_stream_;
  callback(status_version_, std::move(status));
}

}  // namespace media_player
