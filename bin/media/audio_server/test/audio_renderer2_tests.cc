// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/test_with_message_loop.h>
#include <media/cpp/fidl.h>
#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

// Base class for tests of the asynchronous AudioRenderer2 interface.
class AudioRenderer2Test : public gtest::TestWithMessageLoop {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_server_.NewRequest());
    ASSERT_TRUE(audio_server_);

    audio_server_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "AudioServer connection lost. Quitting.";
      error_occurred_ = true;
      message_loop_.PostQuitTask();
    });

    audio_server_->CreateRendererV2(audio_renderer_.NewRequest());
    ASSERT_TRUE(audio_renderer_);

    audio_renderer_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "AudioRenderer2 connection lost. Quitting.";
      error_occurred_ = true;
      message_loop_.PostQuitTask();
    });
  }
  void TearDown() override { EXPECT_FALSE(error_occurred_); }

  AudioServerPtr audio_server_;
  AudioRenderer2Ptr audio_renderer_;
  bool error_occurred_ = false;
};

// Basic validation of SetPcmFormat() for the asynchronous AudioRenderer2.
TEST_F(AudioRenderer2Test, SetPcmFormat) {
  AudioPcmFormat format;
  format.sample_format = AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_renderer_->SetPcmFormat(std::move(format));

  int64_t lead_time = -1;
  audio_renderer_->GetMinLeadTime([this, &lead_time](int64_t min_lead_time) {
    lead_time = min_lead_time;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_GE(lead_time, 0);
}

// If renderer is not in operational mode, a second SetPcmFormat should succeed.
TEST_F(AudioRenderer2Test, SetPcmFormat_Double) {
  AudioPcmFormat format;
  format.sample_format = AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_renderer_->SetPcmFormat(std::move(format));

  AudioPcmFormat format2;
  format2.sample_format = AudioSampleFormat::FLOAT;
  format2.channels = 2;
  format2.frames_per_second = 44100;
  audio_renderer_->SetPcmFormat(std::move(format2));

  int64_t lead_time = -1;
  audio_renderer_->GetMinLeadTime([this, &lead_time](int64_t min_lead_time) {
    lead_time = min_lead_time;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(100)));
  EXPECT_GE(lead_time, 0);
}

// Base class for tests of the synchronous AudioRenderer2Sync interface.
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
//
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
class AudioRenderer2SyncTest : public gtest::TestWithMessageLoop {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_server_.NewRequest());
    ASSERT_TRUE(audio_server_);

    ASSERT_TRUE(audio_server_->CreateRendererV2(audio_renderer_.NewRequest()));
    ASSERT_TRUE(audio_renderer_);
  }

  AudioServerSyncPtr audio_server_;
  AudioRenderer2SyncPtr audio_renderer_;
};

// Basic validation of SetPcmFormat() for the synchronous AudioRenderer2.
TEST_F(AudioRenderer2SyncTest, SetPcmFormat) {
  AudioPcmFormat format;
  format.sample_format = AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_TRUE(audio_renderer_->SetPcmFormat(std::move(format)));

  int64_t min_lead_time = -1;
  ASSERT_TRUE(audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

// If renderer is not in operational mode, a second SetPcmFormat should succeed.
TEST_F(AudioRenderer2SyncTest, SetPcmFormat_Double) {
  AudioPcmFormat format;
  format.sample_format = AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_TRUE(audio_renderer_->SetPcmFormat(std::move(format)));

  AudioPcmFormat format2;
  format2.sample_format = AudioSampleFormat::SIGNED_16;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  EXPECT_TRUE(audio_renderer_->SetPcmFormat(std::move(format2)));

  int64_t min_lead_time = -1;
  ASSERT_TRUE(audio_renderer_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

}  // namespace test
}  // namespace audio
}  // namespace media
