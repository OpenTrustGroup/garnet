// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/real_loop_fixture.h>

#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

// Base class for tests of the asynchronous AudioOut interface.
class AudioOutTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "Audio connection lost. Quitting.";
      error_occurred_ = true;
      QuitLoop();
    });

    audio_->CreateAudioOut(audio_out_.NewRequest());
    ASSERT_TRUE(audio_out_);

    audio_out_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "AudioOut connection lost. Quitting.";
      error_occurred_ = true;
      QuitLoop();
    });
  }
  void TearDown() override { EXPECT_FALSE(error_occurred_); }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioOutPtr audio_out_;
  bool error_occurred_ = false;
};

// Basic validation of SetPcmStreamType() for the asynchronous AudioOut.
TEST_F(AudioOutTest, SetPcmStreamType) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_out_->SetPcmStreamType(std::move(format));

  int64_t lead_time = -1;
  audio_out_->GetMinLeadTime([this, &lead_time](int64_t min_lead_time) {
    lead_time = min_lead_time;
    QuitLoop();
  });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_GE(lead_time, 0);
}

// If an AudioOut is not in operational mode, a second SetPcmStreamType should
// succeed.
TEST_F(AudioOutTest, SetPcmFormat_Double) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  audio_out_->SetPcmStreamType(std::move(format));

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format2.channels = 2;
  format2.frames_per_second = 44100;
  audio_out_->SetPcmStreamType(std::move(format2));

  int64_t lead_time = -1;
  audio_out_->GetMinLeadTime([this, &lead_time](int64_t min_lead_time) {
    lead_time = min_lead_time;
    QuitLoop();
  });

  EXPECT_FALSE(RunLoopWithTimeout(zx::msec(100)));
  EXPECT_GE(lead_time, 0);
}

// Base class for tests of the synchronous AudioOutSync interface.
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
//
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
class AudioOutSyncTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    ASSERT_EQ(ZX_OK, audio_->CreateAudioOut(audio_out_.NewRequest()));
    ASSERT_TRUE(audio_out_);
  }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioSyncPtr audio_;
  fuchsia::media::AudioOutSyncPtr audio_out_;
};

// Basic validation of SetPcmStreamType() for the synchronous AudioOut.
TEST_F(AudioOutSyncTest, SetPcmStreamType) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_EQ(ZX_OK, audio_out_->SetPcmStreamType(std::move(format)));

  int64_t min_lead_time = -1;
  ASSERT_EQ(ZX_OK, audio_out_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

// If an AudioOut is not in operational mode, a second SetPcmStreamType should
// succeed.
TEST_F(AudioOutSyncTest, SetPcmFormat_Double) {
  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  format.channels = 2;
  format.frames_per_second = 48000;
  EXPECT_EQ(ZX_OK, audio_out_->SetPcmStreamType(std::move(format)));

  fuchsia::media::AudioStreamType format2;
  format2.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format2.channels = 1;
  format2.frames_per_second = 44100;
  EXPECT_EQ(ZX_OK, audio_out_->SetPcmStreamType(std::move(format2)));

  int64_t min_lead_time = -1;
  EXPECT_EQ(ZX_OK, audio_out_->GetMinLeadTime(&min_lead_time));
  EXPECT_GE(min_lead_time, 0);
}

}  // namespace test
}  // namespace audio
}  // namespace media
