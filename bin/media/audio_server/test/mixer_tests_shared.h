// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"
#include "garnet/bin/media/audio_server/test/audio_analysis.h"
#include "gtest/gtest.h"

namespace media {
namespace audio {
namespace test {

//
// Subtest shared helper functions -- used by tests; can ASSERT on their own.
//

// Converts a gain multiplier (in fixed-pt 4.28) to decibels (in double floating
// point). Here, dB refers to Power, so 10x change is +20 dB (not +10dB).
inline double GainScaleToDb(Gain::AScale gain_scale) {
  return ValToDb(static_cast<double>(gain_scale) / Gain::kUnityScale);
}

// Find a suitable mixer for the provided format, channels and frame rates.
MixerPtr SelectMixer(AudioSampleFormat src_format,
                     uint32_t src_channels,
                     uint32_t src_frame_rate,
                     uint32_t dst_channels,
                     uint32_t dst_frame_rate,
                     Mixer::Resampler resampler = Mixer::Resampler::Default);

// OutputFormatters convert frames from accumulation format to dest format.
OutputFormatterPtr SelectOutputFormatter(AudioSampleFormat dst_format,
                                         uint32_t num_channels);

// Use supplied mixer to mix (w/out rate conversion) from source to accumulator.
// TODO(mpuryear): refactor this so that tests just call mixer->Mix directly.
void DoMix(MixerPtr mixer,
           const void* src_buf,
           int32_t* accum_buf,
           bool accumulate,
           int32_t num_frames,
           Gain::AScale mix_scale = Gain::kUnityScale);

}  // namespace test
}  // namespace audio
}  // namespace media
