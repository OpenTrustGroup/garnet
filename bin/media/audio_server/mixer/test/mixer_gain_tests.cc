// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_server/mixer/test/audio_result.h"
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

//
// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. From a data scaling standpoint, is our scaling accurately performed,
// and is it adequately linear? Do our gains and accumulators behave as expected
// when they overflow?
//
// Gain tests using the Gain and AScale objects only
//
// Test the internally-used inline func that converts fixed-point gain to dB.
TEST(Gain, GainScaleToDb) {
  // Unity scale is 0.0dB (no change).
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale), 0.0f);

  // 10x scale-up in amplitude (by definition) is exactly +20.0dB.
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale * 10), 20.0f);

  float gain_db = GainScaleToDb(Gain::kUnityScale / 100);
  // 1/100x scale-down in amplitude (by definition) is exactly -40.0dB.
  EXPECT_EQ(gain_db, -40.0f);

  gain_db = GainScaleToDb(Gain::kUnityScale >> 1);
  // 1/2x scale-down in amplitude (by calculation) is -6.02059991327962...dB.
  EXPECT_EQ(gain_db, -6.020600f);  // that val, precise to float32 limitations.
}

// Do renderer and output gains correctly combine to produce unity scaling?
TEST(Gain, Unity) {
  Gain gain;
  Gain::AScale amplitude_scale;

  gain.SetRendererGain(0.0f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(Gain::kMaxGain / 2);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGain / 2);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // These positive/negative values should sum to 0.0: UNITY
  gain.SetRendererGain(Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(-Gain::kMaxGain);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);
}

// Gain caches any previously set Renderer gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST(Gain, Caching) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetRendererGain(6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale(-12.0f);

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  amplitude_scale = gain.GetGainScale(-6.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different Renderer gain that will be cached (+3.0)
  gain.SetRendererGain(3.0f);
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  amplitude_scale = gain.GetGainScale(-9.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);
}

// System independently limits RendererGain to kMaxGain (24 dB) and OutputGain
// to 0, intending for their sum to fit into a fixed-point (4.28) container.
// MTWN-70 relates to Gain's statefulness. Does it need this complexity?
TEST(Gain, MaxClamp) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale;

  // RendererGain of 2 * kMaxGain is clamped to kMaxGain (+24 dB).
  gain.SetRendererGain(Gain::kMaxGain * 2);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  // System limits RendererGain to kMaxGain, even when the sum is less than 0.
  // RenderGain +36dB (clamped to +24dB) plus OutputGain -48dB becomes -24dB.
  gain.SetRendererGain(Gain::kMaxGain * 1.5f);
  amplitude_scale = gain.GetGainScale(-2 * Gain::kMaxGain);
  // A gain_scale value of 0x10270AC represents -24.0dB.
  EXPECT_EQ(0x10270ACu, amplitude_scale);

  // This combination (24.05 dB) would even fit into 4.24, but clamps to 24.0dB.
  gain.SetRendererGain(Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(0.05f);
  EXPECT_EQ(Gain::kMaxScale, amplitude_scale);

  // System limits OutputGain to 0, independent of renderer gain.
  // RendGain = -kMaxGain, OutGain = 1.0 (limited to 0). Expect -kMaxGain.
  gain.SetRendererGain(-Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(1.0);
  EXPECT_EQ(0x10270ACu, amplitude_scale);
}

// System independently limits RendererGain and OutputGain to kMinGain (-160dB).
// Is scale set to zero, if either (or the combo) is at or below kMinGain?
TEST(Gain, MinMute) {
  Gain gain;
  Gain::AScale amplitude_scale;

  // if OutputGain <= kMinGain, scale must be 0, regardless of RendererGain
  gain.SetRendererGain(-2 * Gain::kMinGain);
  amplitude_scale = gain.GetGainScale(Gain::kMinGain);
  EXPECT_EQ(0u, amplitude_scale);

  // if RendererGain <= kMinGain, scale must be 0, regardless of OutputGain
  gain.SetRendererGain(Gain::kMinGain);
  amplitude_scale = gain.GetGainScale(Gain::kMaxGain * 1.2);
  EXPECT_EQ(0u, amplitude_scale);

  // if sum of RendererGain and OutputGain <= kMinGain, scale should be 0
  // Output gain is just slightly above MinGain, and Render takes us below it
  gain.SetRendererGain(-2.0f);
  amplitude_scale = gain.GetGainScale(Gain::kMinGain + 1.0f);
  EXPECT_EQ(0u, amplitude_scale);
}

// Does GetGainScale round appropriately when converting dB into AScale?
// SetRendererGain just saves the given float; GetGainScale produces a
// fixed-point uint32 (4.28 format), truncating (not rounding) in the process.
TEST(Gain, Precision) {
  Gain gain;
  gain.SetRendererGain(-159.99f);
  Gain::AScale amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0x00000003u, amplitude_scale);  // ...2.68 rounds up to ...3

  gain.SetRendererGain(-157.696f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0x00000003u, amplitude_scale);  // ...3.499 rounds down to ...3

  gain.SetRendererGain(-0.50f);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0x0F1ADF94u, amplitude_scale);  // ...F93.8 rounds to ...F94

  gain.SetRendererGain(Gain::kMaxGain);
  amplitude_scale = gain.GetGainScale(0.0f);
  EXPECT_EQ(0xFD9539A4u, amplitude_scale);  // ...A4.4 rounds down to ...A4
}

//
// Data scaling tests
//
// These validate the actual scaling of audio data, including overflow and any
// truncation or rounding (above just checks the generation of scale values).
//
// When doing direct bit-for-bit comparisons in these tests, we must factor in
// the left-shift biasing that is done while converting input data into the
// internal format of our accumulator. For this reason, all "expect" values are
// specified at a higher-than-needed precision of 24-bit, and then normalized
// down to the actual pipeline width.

// Verify whether per-stream gain interacts linearly with accumulation buffer.
TEST(Gain, Scaling_Linearity) {
  int16_t source[] = {0x0CE4, 0x0CCC, 0x23, 4, -0x0E, -0x19, -0x0CCC, -0x0CDB};
  int32_t accum[8];
  Gain gain;

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  gain.SetRendererGain(20.0f);
  Gain::AScale stream_scale = gain.GetGainScale(0.0f);

  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                               Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  int32_t expect[] = {0x80E800, 0x7FF800, 0x15E00,   0x2800,
                      -0x8C00,  -0xFA00,  -0x7FF800, -0x808E00};
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -20.00 dB leads to exactly 0.10x in value
  gain.SetRendererGain(-20.0f);
  stream_scale = gain.GetGainScale(0.0f);

  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 1, 44100,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_scale);

  int32_t expect2[] = {0x14A00, 0x14799, 0x380,    0x66,
                       -0x166,  -0x280,  -0x14799, -0x14919};
  NormalizeInt24ToPipelineBitwidth(expect2, fbl::count_of(expect2));
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our Gain respond to very low values? Today during the scaling
// process, the system should round fractional data values away from 0.
// By "round away from zero", we mean: 1.5 --> 2; -1.5 --> -2; -1.1 --> -1.
TEST(Gain, Scaling_Precision) {
  int16_t source[] = {0x7FFF, -0x8000, -1, 1};  // max/min values
  int32_t accum[4];

  // Before, even slightly below unity reduced all positive vals. Now we round.
  // For this reason, at this gain_scale, resulting audio should be unchanged.
  Gain::AScale gain_scale = AudioResult::kPrevScaleEpsilon + 1;
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  int32_t expect[] = {0x7FFF00, -0x800000, -0x100, 0x100};
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // This gain is the first (closest-to-unity) to change a full-scale signal.
  gain_scale = AudioResult::kPrevScaleEpsilon;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  expect[0]--;
  expect[1]++;
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // This is lowest gain_scale that produces non-zero from full-scale.
  // Why "+1"? Differences in negative and positive range; see subsequent check.
  gain_scale = AudioResult::kPrevMinScaleNonZero + 1;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  int32_t expect2[] = {1, -1, 0, 0};
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));

  // This 'special' scale straddles boundaries: 32767 is reduced to _just_ less
  // than .5 (and rounds in) while -32768 becomes -.50000 (rounding out to -1).
  gain_scale = AudioResult::kPrevMinScaleNonZero;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        gain_scale);

  expect2[0] = 0;
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));

  // At this gain, even -32768 is reduced to -.49... thus rounds in to 0.
  // Therefore, nothing should change in the accumulator buffer.
  gain_scale = AudioResult::kPrevMinScaleNonZero - 1;
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        gain_scale);

  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(Gain, Accumulator) {
  int16_t source[] = {0x7FFF, -0x8000};
  int32_t accum[] = {0x7FFF00, -0x800000};
  int32_t expect[] = {0xFFFE00, -0x1000000};
  int32_t expect2[] = {0x17FFD00, -0x1800000};

  // When mixed, these far exceed any int16 range
  NormalizeInt24ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt24ToPipelineBitwidth(expect, fbl::count_of(expect));
  NormalizeInt24ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  // These values exceed the per-stream range of int16
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // these values even exceed uint16
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, 1);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our accumulator behave at its limits? Does it clamp or rollover?
TEST(Gain, Accumulator_Clamp) {
  int16_t source[] = {0x7FFF, -0x8000};
  // This mix will exceed int32 max and min respectively: accum SHOULD clamp.
  int32_t accum[] = {std::numeric_limits<int32_t>::max() -
                         (source[0] << (kAudioPipelineWidth - 16)) + 1,
                     std::numeric_limits<int32_t>::min() -
                         (source[1] << (kAudioPipelineWidth - 16)) - 1};

  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));

  // TODO(mpuryear): when MTWN-83 is fixed, expect max and min (not min & max).
  int32_t expect[] = {std::numeric_limits<int32_t>::min(),
                      std::numeric_limits<int32_t>::max()};
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Our mixer contains an optimization in which it skips mixing operations if it
// detects that gain is below a certain threshold (regardless of "accumulate").
TEST(Gain, Accumulator_Clear) {
  int16_t source[] = {-32768, 32767};
  int32_t accum[] = {-32768, 32767};
  int32_t expect[] = {-32768, 32767};

  // We will test both SampleAndHold and LinearInterpolation interpolators.
  MixerPtr mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                               Resampler::SampleAndHold);
  // Use the gain guaranteed to silence all signals: Gain::MuteThreshold.
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // Try with the other sampler.
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::LinearInterpolation);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // When accumulate = false, this is overridden: it behaves identically.
  //
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // Ensure that both samplers behave identically in this regard.
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000,
                      Resampler::LinearInterpolation);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        Gain::MuteThreshold());
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Headroom - post-SUM gain
// TODO(mpuryear): When we have a master gain stage that can take advantage of
// the headroom inherent in a multi-stream accumulator, implement this test.

}  // namespace test
}  // namespace audio
}  // namespace media
