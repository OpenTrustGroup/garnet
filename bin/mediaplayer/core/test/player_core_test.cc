// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/core/player_core.h"

#include "garnet/bin/mediaplayer/core/demux_source_segment.h"
#include "garnet/bin/mediaplayer/core/renderer_sink_segment.h"
#include "garnet/bin/mediaplayer/core/test/fake_audio_renderer.h"
#include "garnet/bin/mediaplayer/core/test/fake_demux.h"
#include "garnet/bin/mediaplayer/core/test/fake_sink_segment.h"
#include "garnet/bin/mediaplayer/core/test/fake_source_segment.h"
#include "garnet/bin/mediaplayer/core/test/fake_video_renderer.h"
#include "garnet/bin/mediaplayer/graph/formatting.h"
#include "lib/gtest/test_loop_fixture.h"

namespace media_player {
namespace test {

std::ostream& operator<<(std::ostream& os, NodeRef value) {
  if (!value) {
    return os << "<none>";
  }

  os << value.GetGenericNode()->label();
  for (size_t output_index = 0; output_index < value.output_count();
       output_index++) {
    os << fostr::NewLine << "[" << output_index << "] " << fostr::Indent
       << value.output(output_index).mate().node() << fostr::Outdent;
  }

  return os;
}

void ExpectEqual(const StreamType* a, const StreamType* b) {
  if (!a) {
    EXPECT_EQ(nullptr, b);
    return;
  }

  EXPECT_NE(nullptr, b);

  EXPECT_EQ(a->medium(), b->medium());
  EXPECT_EQ(a->encoding(), b->encoding());

  switch (a->medium()) {
    case StreamType::Medium::kAudio:
      EXPECT_EQ(StreamType::Medium::kAudio, b->medium());
      EXPECT_NE(nullptr, a->audio());
      EXPECT_NE(nullptr, b->audio());
      EXPECT_EQ(a->audio()->sample_format(), b->audio()->sample_format());
      EXPECT_EQ(a->audio()->channels(), b->audio()->channels());
      EXPECT_EQ(a->audio()->frames_per_second(),
                b->audio()->frames_per_second());
      break;
    case StreamType::Medium::kVideo:
      EXPECT_EQ(StreamType::Medium::kVideo, b->medium());
      EXPECT_NE(nullptr, a->video());
      EXPECT_NE(nullptr, b->video());
      EXPECT_EQ(a->video()->profile(), b->video()->profile());
      EXPECT_EQ(a->video()->pixel_format(), b->video()->pixel_format());
      EXPECT_EQ(a->video()->color_space(), b->video()->color_space());
      EXPECT_EQ(a->video()->width(), b->video()->width());
      EXPECT_EQ(a->video()->height(), b->video()->height());
      EXPECT_EQ(a->video()->coded_width(), b->video()->coded_width());
      EXPECT_EQ(a->video()->coded_height(), b->video()->coded_height());
      EXPECT_EQ(a->video()->pixel_aspect_ratio_width(),
                b->video()->pixel_aspect_ratio_width());
      EXPECT_EQ(a->video()->pixel_aspect_ratio_height(),
                b->video()->pixel_aspect_ratio_height());
      break;
    case StreamType::Medium::kText:
      EXPECT_EQ(StreamType::Medium::kText, b->medium());
      EXPECT_NE(nullptr, a->text());
      EXPECT_NE(nullptr, b->text());
      break;
    case StreamType::Medium::kSubpicture:
      EXPECT_EQ(StreamType::Medium::kSubpicture, b->medium());
      EXPECT_NE(nullptr, a->subpicture());
      EXPECT_NE(nullptr, b->subpicture());
      break;
  }
}

void ExpectNoStreams(const PlayerCore& player_core, StreamType::Medium medium) {
  EXPECT_FALSE(player_core.has_sink_segment(medium));
  EXPECT_FALSE(player_core.content_has_medium(medium));
  EXPECT_FALSE(player_core.medium_connected(medium));
}

void ExpectNoStreams(const PlayerCore& player_core) {
  ExpectNoStreams(player_core, StreamType::Medium::kAudio);
  ExpectNoStreams(player_core, StreamType::Medium::kVideo);
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);
}

using PlayerTest = ::gtest::TestLoopFixture;

// Tests that a fresh player_core responds to simple queries as expected.
TEST_F(PlayerTest, FreshPlayer) {
  PlayerCore player_core(dispatcher());

  EXPECT_FALSE(player_core.has_source_segment());

  ExpectNoStreams(player_core);

  EXPECT_FALSE(player_core.end_of_stream());
  EXPECT_EQ(nullptr, player_core.metadata());
  EXPECT_EQ(nullptr, player_core.problem());
  EXPECT_NE(nullptr, player_core.graph());
  EXPECT_EQ(NodeRef(), player_core.source_node());
}

// Tests that SetSourceSegment calls back immediately if a null source segment
// is set.
TEST_F(PlayerTest, NullSourceSegment) {
  PlayerCore player_core(dispatcher());

  bool set_source_segment_callback_called = false;

  player_core.SetSourceSegment(nullptr,
                               [&set_source_segment_callback_called]() {
                                 set_source_segment_callback_called = true;
                               });

  RunLoopUntilIdle();
  EXPECT_TRUE(set_source_segment_callback_called);
  EXPECT_FALSE(player_core.has_source_segment());
  ExpectNoStreams(player_core);
}

// Tests the player_core by setting up a fake source segment and two fake sink
// segments, exercising the player_core and then removing the segments.
TEST_F(PlayerTest, FakeSegments) {
  PlayerCore player_core(dispatcher());

  bool update_callback_called = false;
  player_core.SetUpdateCallback(
      [&update_callback_called]() { update_callback_called = true; });

  EXPECT_FALSE(update_callback_called);

  // Add a source segment.
  bool source_segment_destroyed;
  std::unique_ptr<FakeSourceSegment> source_segment = FakeSourceSegment::Create(
      [&source_segment_destroyed](FakeSourceSegment* source_segment) {
        source_segment_destroyed = true;
        EXPECT_TRUE(source_segment->will_deprovision_called_);
        EXPECT_FALSE(source_segment->TEST_provisioned());
      });
  FakeSourceSegment* source_segment_raw = source_segment.get();

  EXPECT_FALSE(source_segment_raw->did_provision_called_);
  EXPECT_FALSE(source_segment_raw->will_deprovision_called_);
  EXPECT_FALSE(source_segment_raw->TEST_provisioned());

  EXPECT_FALSE(player_core.has_source_segment());

  bool set_source_segment_callback_called = false;
  player_core.SetSourceSegment(std::move(source_segment),
                               [&set_source_segment_callback_called]() {
                                 set_source_segment_callback_called = true;
                               });

  EXPECT_TRUE(player_core.has_source_segment());
  EXPECT_FALSE(update_callback_called);
  EXPECT_FALSE(set_source_segment_callback_called);
  EXPECT_TRUE(source_segment_raw->did_provision_called_);
  source_segment_raw->did_provision_called_ = false;
  EXPECT_FALSE(source_segment_raw->will_deprovision_called_);
  EXPECT_TRUE(source_segment_raw->TEST_provisioned());

  ExpectNoStreams(player_core);

  // Add an audio stream indicating we will add more streams.
  AudioStreamType audio_type(StreamType::kAudioEncodingLpcm, nullptr,
                             AudioStreamType::SampleFormat::kSigned16, 2,
                             44100);

  // We need a non-null output, but it doesn't have to work.
  OutputRef audio_output(reinterpret_cast<Output*>(1));

  source_segment_raw->TEST_OnStreamUpdated(0, audio_type, audio_output, true);

  EXPECT_FALSE(update_callback_called);
  EXPECT_FALSE(set_source_segment_callback_called);

  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kAudio));
  ExpectNoStreams(player_core, StreamType::Medium::kVideo);
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  // Add a video stream indicating we will *not* add more streams.
  VideoStreamType video_type(StreamType::kVideoEncodingUncompressed, nullptr,
                             VideoStreamType::VideoProfile::kNotApplicable,
                             VideoStreamType::PixelFormat::kYv12,
                             VideoStreamType::ColorSpace::kNotApplicable, 0, 0,
                             0, 0, 1, 1, {}, {});

  // We need a non-null output, but it doesn't have to work.
  OutputRef video_output(reinterpret_cast<Output*>(2));

  source_segment_raw->TEST_OnStreamUpdated(1, video_type, video_output, false);

  EXPECT_FALSE(update_callback_called);
  EXPECT_TRUE(set_source_segment_callback_called);

  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  // Make sure notification works via the source.
  source_segment_raw->TEST_NotifyUpdate();
  EXPECT_TRUE(update_callback_called);
  update_callback_called = false;

  EXPECT_FALSE(player_core.end_of_stream());
  EXPECT_EQ(nullptr, player_core.metadata());
  EXPECT_EQ(nullptr, player_core.problem());

  // Make sure problem reporting works via the source.
  source_segment_raw->TEST_ReportProblem("fake problem type",
                                         "fake problem details");
  EXPECT_TRUE(update_callback_called);
  update_callback_called = false;
  EXPECT_NE(nullptr, player_core.problem());
  EXPECT_EQ("fake problem type", player_core.problem()->type);
  EXPECT_EQ("fake problem details", player_core.problem()->details);

  source_segment_raw->TEST_ReportNoProblem();
  EXPECT_TRUE(update_callback_called);
  update_callback_called = false;
  EXPECT_EQ(nullptr, player_core.problem());

  // Make sure metadata works via the source.
  EXPECT_EQ(nullptr, player_core.metadata());
  Metadata metadata;
  metadata.emplace("title", "fake title");
  metadata.emplace("artist", "fake artist");
  metadata.emplace("album", "fake album");
  metadata.emplace("publisher", "fake publisher");
  metadata.emplace("genre", "fake genre");
  metadata.emplace("composer", "fake composer");
  source_segment_raw->metadata_ = &metadata;
  EXPECT_EQ(&metadata, player_core.metadata());

  // Add a sink segment for audio.
  bool audio_sink_segment_destroyed;
  std::unique_ptr<FakeSinkSegment> audio_sink_segment = FakeSinkSegment::Create(
      [&audio_sink_segment_destroyed](FakeSinkSegment* sink_segment) {
        audio_sink_segment_destroyed = true;
        EXPECT_TRUE(sink_segment->unprepare_called_);
        EXPECT_TRUE(sink_segment->disconnect_called_);
        EXPECT_TRUE(sink_segment->will_deprovision_called_);
        EXPECT_FALSE(sink_segment->TEST_provisioned());
      });
  FakeSinkSegment* audio_sink_segment_raw = audio_sink_segment.get();

  EXPECT_FALSE(audio_sink_segment_raw->did_provision_called_);
  EXPECT_FALSE(audio_sink_segment_raw->will_deprovision_called_);
  EXPECT_FALSE(audio_sink_segment_raw->TEST_provisioned());

  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kAudio));

  player_core.SetSinkSegment(std::move(audio_sink_segment),
                             StreamType::Medium::kAudio);

  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_FALSE(update_callback_called);
  EXPECT_TRUE(audio_sink_segment_raw->did_provision_called_);
  audio_sink_segment_raw->did_provision_called_ = false;
  EXPECT_FALSE(audio_sink_segment_raw->will_deprovision_called_);
  EXPECT_TRUE(audio_sink_segment_raw->TEST_provisioned());

  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  EXPECT_TRUE(audio_sink_segment_raw->connect_called_);
  audio_sink_segment_raw->connect_called_ = false;
  EXPECT_FALSE(audio_sink_segment_raw->prepare_called_);
  EXPECT_NE(nullptr, audio_sink_segment_raw->connect_call_param_type_);
  ExpectEqual(&audio_type, audio_sink_segment_raw->connect_call_param_type_);
  EXPECT_NE(nullptr, audio_sink_segment_raw->connect_call_param_callback_);

  audio_sink_segment_raw->connected_ = true;
  audio_sink_segment_raw->connect_call_param_callback_(Result::kOk);
  EXPECT_TRUE(audio_sink_segment_raw->prepare_called_);
  audio_sink_segment_raw->prepare_called_ = false;

  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  // Add a sink segment for video.
  bool video_sink_segment_destroyed;
  std::unique_ptr<FakeSinkSegment> video_sink_segment = FakeSinkSegment::Create(
      [&video_sink_segment_destroyed](FakeSinkSegment* sink_segment) {
        video_sink_segment_destroyed = true;
        EXPECT_TRUE(sink_segment->unprepare_called_);
        EXPECT_TRUE(sink_segment->disconnect_called_);
        EXPECT_TRUE(sink_segment->will_deprovision_called_);
        EXPECT_FALSE(sink_segment->TEST_provisioned());
      });
  FakeSinkSegment* video_sink_segment_raw = video_sink_segment.get();

  EXPECT_FALSE(video_sink_segment_raw->did_provision_called_);
  EXPECT_FALSE(video_sink_segment_raw->will_deprovision_called_);
  EXPECT_FALSE(video_sink_segment_raw->TEST_provisioned());

  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kVideo));

  player_core.SetSinkSegment(std::move(video_sink_segment),
                             StreamType::Medium::kVideo);

  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_FALSE(update_callback_called);
  EXPECT_TRUE(video_sink_segment_raw->did_provision_called_);
  video_sink_segment_raw->did_provision_called_ = false;
  EXPECT_FALSE(video_sink_segment_raw->will_deprovision_called_);
  EXPECT_TRUE(video_sink_segment_raw->TEST_provisioned());

  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  EXPECT_TRUE(video_sink_segment_raw->connect_called_);
  video_sink_segment_raw->connect_called_ = false;
  EXPECT_FALSE(video_sink_segment_raw->prepare_called_);
  EXPECT_NE(nullptr, video_sink_segment_raw->connect_call_param_type_);
  ExpectEqual(&video_type, video_sink_segment_raw->connect_call_param_type_);
  EXPECT_NE(nullptr, video_sink_segment_raw->connect_call_param_callback_);

  video_sink_segment_raw->connected_ = true;
  video_sink_segment_raw->connect_call_param_callback_(Result::kOk);
  EXPECT_TRUE(video_sink_segment_raw->prepare_called_);
  video_sink_segment_raw->prepare_called_ = false;

  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  // Test Prime.
  EXPECT_FALSE(audio_sink_segment_raw->prime_called_);
  EXPECT_FALSE(video_sink_segment_raw->prime_called_);
  bool prime_callback_called = false;
  player_core.Prime(
      [&prime_callback_called]() { prime_callback_called = true; });
  EXPECT_FALSE(prime_callback_called);
  EXPECT_TRUE(audio_sink_segment_raw->prime_called_);
  audio_sink_segment_raw->prime_called_ = false;
  EXPECT_TRUE(video_sink_segment_raw->prime_called_);
  video_sink_segment_raw->prime_called_ = false;
  EXPECT_NE(nullptr, audio_sink_segment_raw->prime_call_param_callback_);
  EXPECT_NE(nullptr, video_sink_segment_raw->prime_call_param_callback_);

  audio_sink_segment_raw->prime_call_param_callback_();
  EXPECT_FALSE(prime_callback_called);

  video_sink_segment_raw->prime_call_param_callback_();
  RunLoopUntilIdle();
  EXPECT_TRUE(prime_callback_called);

  // Test Flush.
  EXPECT_FALSE(source_segment_raw->flush_called_);
  bool flush_callback_called = false;
  player_core.Flush(
      true, [&flush_callback_called]() { flush_callback_called = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(flush_callback_called);
  EXPECT_TRUE(source_segment_raw->flush_called_);
  source_segment_raw->flush_called_ = false;
  EXPECT_EQ(true, source_segment_raw->flush_call_param_hold_frame_);

  // Test SetTimelineFunction.
  EXPECT_FALSE(audio_sink_segment_raw->set_timeline_function_called_);
  EXPECT_FALSE(video_sink_segment_raw->set_timeline_function_called_);
  media::TimelineFunction timeline_function(1, 2, 3, 4);
  bool set_timeline_function_callback_called = false;
  player_core.SetTimelineFunction(
      timeline_function, [&set_timeline_function_callback_called]() {
        set_timeline_function_callback_called = true;
      });
  EXPECT_FALSE(set_timeline_function_callback_called);
  EXPECT_TRUE(audio_sink_segment_raw->set_timeline_function_called_);
  audio_sink_segment_raw->set_timeline_function_called_ = false;
  EXPECT_TRUE(video_sink_segment_raw->set_timeline_function_called_);
  video_sink_segment_raw->set_timeline_function_called_ = false;
  EXPECT_EQ(timeline_function,
            audio_sink_segment_raw
                ->set_timeline_function_call_param_timeline_function_);
  EXPECT_EQ(timeline_function,
            video_sink_segment_raw
                ->set_timeline_function_call_param_timeline_function_);
  EXPECT_NE(nullptr,
            audio_sink_segment_raw->set_timeline_function_call_param_callback_);
  EXPECT_NE(nullptr,
            video_sink_segment_raw->set_timeline_function_call_param_callback_);

  audio_sink_segment_raw->set_timeline_function_call_param_callback_();
  EXPECT_FALSE(set_timeline_function_callback_called);

  video_sink_segment_raw->set_timeline_function_call_param_callback_();
  RunLoopUntilIdle();
  EXPECT_TRUE(set_timeline_function_callback_called);
  EXPECT_EQ(timeline_function, player_core.timeline_function());

  // Test SetProgramRange.
  EXPECT_FALSE(audio_sink_segment_raw->set_program_range_called_);
  EXPECT_FALSE(video_sink_segment_raw->set_program_range_called_);
  player_core.SetProgramRange(0, 1, 2);
  EXPECT_TRUE(audio_sink_segment_raw->set_program_range_called_);
  audio_sink_segment_raw->set_program_range_called_ = false;
  EXPECT_TRUE(video_sink_segment_raw->set_program_range_called_);
  video_sink_segment_raw->set_program_range_called_ = false;
  EXPECT_EQ(0u, audio_sink_segment_raw->set_program_range_call_param_program_);
  EXPECT_EQ(0u, video_sink_segment_raw->set_program_range_call_param_program_);
  EXPECT_EQ(1, audio_sink_segment_raw->set_program_range_call_param_min_pts_);
  EXPECT_EQ(1, video_sink_segment_raw->set_program_range_call_param_min_pts_);
  EXPECT_EQ(2, audio_sink_segment_raw->set_program_range_call_param_max_pts_);
  EXPECT_EQ(2, video_sink_segment_raw->set_program_range_call_param_max_pts_);

  // Test Seek.
  EXPECT_FALSE(source_segment_raw->seek_called_);
  bool seek_callback_called = false;
  player_core.Seek(1234,
                   [&seek_callback_called]() { seek_callback_called = true; });
  EXPECT_FALSE(seek_callback_called);
  EXPECT_TRUE(source_segment_raw->seek_called_);
  source_segment_raw->seek_called_ = false;
  EXPECT_EQ(1234, source_segment_raw->seek_call_param_position_);
  EXPECT_NE(nullptr, source_segment_raw->seek_call_param_callback_);

  source_segment_raw->seek_call_param_callback_();
  RunLoopUntilIdle();
  EXPECT_TRUE(seek_callback_called);

  // Test end_of_stream.
  EXPECT_FALSE(player_core.end_of_stream());
  audio_sink_segment_raw->end_of_stream_ = true;
  EXPECT_FALSE(player_core.end_of_stream());
  video_sink_segment_raw->end_of_stream_ = true;
  EXPECT_TRUE(player_core.end_of_stream());

  // Remove the sink for audio.
  EXPECT_FALSE(audio_sink_segment_raw->unprepare_called_);
  EXPECT_FALSE(audio_sink_segment_raw->disconnect_called_);
  EXPECT_FALSE(audio_sink_segment_raw->will_deprovision_called_);

  EXPECT_FALSE(audio_sink_segment_destroyed);
  player_core.SetSinkSegment(nullptr, StreamType::Medium::kAudio);
  EXPECT_TRUE(audio_sink_segment_destroyed);
  audio_sink_segment_raw = nullptr;

  // The callback to Create above checks that the source segment was shut down
  // properly.

  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  // Remove the sink for video.
  EXPECT_FALSE(video_sink_segment_raw->unprepare_called_);
  EXPECT_FALSE(video_sink_segment_raw->disconnect_called_);
  EXPECT_FALSE(video_sink_segment_raw->will_deprovision_called_);

  EXPECT_FALSE(video_sink_segment_destroyed);
  player_core.SetSinkSegment(nullptr, StreamType::Medium::kVideo);
  EXPECT_TRUE(video_sink_segment_destroyed);
  video_sink_segment_raw = nullptr;

  // The callback to Create above checks that the source segment was shut down
  // properly.

  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_FALSE(player_core.has_sink_segment(StreamType::Medium::kVideo));
  EXPECT_TRUE(player_core.content_has_medium(StreamType::Medium::kVideo));
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kVideo));
  ExpectNoStreams(player_core, StreamType::Medium::kText);
  ExpectNoStreams(player_core, StreamType::Medium::kSubpicture);

  // Remove the source.
  EXPECT_FALSE(source_segment_raw->flush_called_);

  EXPECT_FALSE(source_segment_destroyed);
  set_source_segment_callback_called = false;
  player_core.SetSourceSegment(nullptr,
                               [&set_source_segment_callback_called]() {
                                 set_source_segment_callback_called = true;
                               });
  RunLoopUntilIdle();
  EXPECT_TRUE(set_source_segment_callback_called);
  EXPECT_TRUE(source_segment_destroyed);
  source_segment_raw = nullptr;

  // The callback to Create above checks that the source segment was shut down
  // properly.

  ExpectNoStreams(player_core);
  EXPECT_FALSE(player_core.end_of_stream());
  EXPECT_EQ(nullptr, player_core.metadata());
  EXPECT_EQ(nullptr, player_core.problem());
  EXPECT_NE(nullptr, player_core.graph());
  EXPECT_EQ(NodeRef(), player_core.source_node());
}

// Expects the player_core to have built a graph based on the fake demux and
// renderers used with real source and sink segments.
void ExpectRealSegmentsGraph(const PlayerCore& player_core) {
  // Check the source (demux) node.
  NodeRef source_node_ref = player_core.source_node();
  EXPECT_TRUE(source_node_ref);
  EXPECT_EQ(0u, source_node_ref.input_count());
  EXPECT_EQ(2u, source_node_ref.output_count());
  EXPECT_EQ("FakeDemux", source_node_ref.GetGenericNode()->label());

  // Walk the audio segment. It has a decoder, an lpcm reformatter and a
  // renderer.
  NodeRef audio_decoder_node_ref = source_node_ref.output(0).mate().node();
  EXPECT_TRUE(audio_decoder_node_ref);
  EXPECT_EQ(1u, audio_decoder_node_ref.input_count());
  EXPECT_EQ(1u, audio_decoder_node_ref.output_count());
  EXPECT_TRUE(audio_decoder_node_ref.input().connected());
  EXPECT_TRUE(audio_decoder_node_ref.input().prepared());
  EXPECT_TRUE(audio_decoder_node_ref.output().connected());
  EXPECT_EQ("FakeDecoder", audio_decoder_node_ref.GetGenericNode()->label());

  NodeRef audio_renderer_node_ref =
      audio_decoder_node_ref.output().mate().node();
  EXPECT_TRUE(audio_renderer_node_ref);
  EXPECT_EQ(1u, audio_renderer_node_ref.input_count());
  EXPECT_EQ(0u, audio_renderer_node_ref.output_count());
  EXPECT_TRUE(audio_renderer_node_ref.input().connected());
  EXPECT_TRUE(audio_renderer_node_ref.input().prepared());
  EXPECT_EQ("FakeAudioRenderer",
            audio_renderer_node_ref.GetGenericNode()->label());

  // Walk the video segment. It has a decoder and a renderer.
  NodeRef video_decoder_node_ref = source_node_ref.output(1).mate().node();
  EXPECT_TRUE(video_decoder_node_ref);
  EXPECT_EQ(1u, video_decoder_node_ref.input_count());
  EXPECT_EQ(1u, video_decoder_node_ref.output_count());
  EXPECT_TRUE(video_decoder_node_ref.input().connected());
  EXPECT_TRUE(video_decoder_node_ref.input().prepared());
  EXPECT_TRUE(video_decoder_node_ref.output().connected());
  EXPECT_EQ("FakeDecoder", video_decoder_node_ref.GetGenericNode()->label());

  NodeRef video_renderer_node_ref =
      video_decoder_node_ref.output().mate().node();
  EXPECT_TRUE(video_renderer_node_ref);
  EXPECT_EQ(1u, video_renderer_node_ref.input_count());
  EXPECT_EQ(0u, video_renderer_node_ref.output_count());
  EXPECT_TRUE(video_renderer_node_ref.input().connected());
  EXPECT_TRUE(video_renderer_node_ref.input().prepared());
  EXPECT_EQ("FakeVideoRenderer",
            video_renderer_node_ref.GetGenericNode()->label());

  // std::cout << "\n" << source_node_ref << "\n\n";
}

// Tests a player_core with real segments constructed source-first.
TEST_F(PlayerTest, BuildGraphWithRealSegmentsSourceFirst) {
  PlayerCore player_core(dispatcher());
  std::unique_ptr<DecoderFactory> decoder_factory =
      DecoderFactory::Create(nullptr);

  player_core.SetSourceSegment(DemuxSourceSegment::Create(FakeDemux::Create()),
                               nullptr);

  player_core.SetSinkSegment(
      RendererSinkSegment::Create(FakeAudioRenderer::Create(),
                                  decoder_factory.get()),
      StreamType::Medium::kAudio);
  RunLoopUntilIdle();
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kAudio));

  player_core.SetSinkSegment(
      RendererSinkSegment::Create(FakeVideoRenderer::Create(),
                                  decoder_factory.get()),
      StreamType::Medium::kVideo);
  RunLoopUntilIdle();
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kVideo));

  ExpectRealSegmentsGraph(player_core);
}

// Tests a player_core with real segments constructed sinks-first.
TEST_F(PlayerTest, BuildGraphWithRealSegmentsSinksFirst) {
  PlayerCore player_core(dispatcher());
  std::unique_ptr<DecoderFactory> decoder_factory =
      DecoderFactory::Create(nullptr);

  player_core.SetSinkSegment(
      RendererSinkSegment::Create(FakeAudioRenderer::Create(),
                                  decoder_factory.get()),
      StreamType::Medium::kAudio);
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kAudio));

  player_core.SetSinkSegment(
      RendererSinkSegment::Create(FakeVideoRenderer::Create(),
                                  decoder_factory.get()),
      StreamType::Medium::kVideo);
  EXPECT_FALSE(player_core.medium_connected(StreamType::Medium::kVideo));

  player_core.SetSourceSegment(DemuxSourceSegment::Create(FakeDemux::Create()),
                               nullptr);
  RunLoopUntilIdle();
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kAudio));
  EXPECT_TRUE(player_core.medium_connected(StreamType::Medium::kVideo));

  ExpectRealSegmentsGraph(player_core);
}

}  // namespace test
}  // namespace media_player
