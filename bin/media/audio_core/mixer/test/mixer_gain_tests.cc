// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "garnet/bin/media/audio_core/mixer/test/audio_result.h"
#include "garnet/bin/media/audio_core/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

//
// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. Is data scaling accurately performed, and is it adequately linear? Do
// our gains and accumulators behave as expected when they overflow?
//
// Gain tests using the Gain and AScale objects only
//
TEST(Bookkeeping, GainDefaults) {
  Gain gain;

  EXPECT_TRUE(gain.IsUnity());
  EXPECT_FALSE(gain.IsSilent());
  EXPECT_EQ(gain.GetGainScale(), Gain::kUnityScale);
}

// Test the internally-used inline func that converts AScale gain to dB.
TEST(Gain, GainScaleToDb) {
  // Unity scale is 0.0dB (no change).
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale), 0.0f);

  // 10x scale-up in amplitude (by definition) is exactly +20.0dB.
  EXPECT_EQ(GainScaleToDb(Gain::kUnityScale * 10.0f), 20.0f);

  // 1/100x scale-down in amplitude (by definition) is exactly -40.0dB.
  EXPECT_EQ(static_cast<float>(GainScaleToDb(Gain::kUnityScale * 0.01f)),
            -40.0f);

  EXPECT_EQ(static_cast<float>(GainScaleToDb(Gain::kUnityScale * 0.5f)),
            -6.020600f);  // 1/2x scale-down by calculation: -6.02059991328..dB.
}

void TestUnityGain(float source_gain_db, float dest_gain_db) {
  Gain gain;

  gain.SetSourceGain(source_gain_db);
  EXPECT_EQ(Gain::kUnityScale, gain.GetGainScale(dest_gain_db));

  gain.SetDestGain(dest_gain_db);
  EXPECT_TRUE(gain.IsUnity());
  EXPECT_FALSE(gain.IsSilent());
}

// Do renderer and output gains correctly combine to produce unity scaling?
TEST(Gain, Unity) {
  TestUnityGain(0.0f, 0.0f);

  // These positive/negative values should sum to 0.0: UNITY
  TestUnityGain(Gain::kMaxGainDb / 2, -Gain::kMaxGainDb / 2);
  TestUnityGain(-Gain::kMaxGainDb, Gain::kMaxGainDb);
}

// Gain caches any previously set renderer gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST(Gain, Caching) {
  Gain gain, expect_gain;
  Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetSourceGain(6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale(-12.0f);

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  amplitude_scale = gain.GetGainScale(-6.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different renderer gain that will be cached (+3.0).
  gain.SetSourceGain(3.0f);
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  amplitude_scale = gain.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  gain.SetDestGain(-9.0f);
  EXPECT_EQ(expect_amplitude_scale, gain.GetGainScale());

  // Render gain cached +3 and Output gain non-cached -3 should lead to Unity.
  EXPECT_EQ(Gain::kUnityScale, gain.GetGainScale(-3.0f));

  // Cached Output gain should still be -9, leading to -6dB.
  EXPECT_EQ(expect_amplitude_scale, gain.GetGainScale());
}

// We independently limit renderer/Output gains to kMaxGainDb/0, respectively.
// MTWN-70 concerns Gain's statefulness. Does it need this complexity?
TEST(Gain, MaxClamp) {
  Gain gain;

  // Renderer Gain of 2 * kMaxGainDb is clamped to kMaxGainDb (+24 dB).
  gain.SetSourceGain(Gain::kMaxGainDb * 2);
  EXPECT_EQ(Gain::kMaxScale, gain.GetGainScale(0.0f));

  // This combination (24.05 dB) is clamped to 24.0dB.
  gain.SetSourceGain(Gain::kMaxGainDb);
  EXPECT_EQ(Gain::kMaxScale, gain.GetGainScale(0.05f));

  // System limits renderer gain to kMaxGainDb, even when sum is less than 0.
  // Renderer Gain +36dB (clamped to +24dB) plus system Gain -48dB ==> -24dB.
  constexpr float kScale24DbDown = 0.0630957344f;
  gain.SetSourceGain(Gain::kMaxGainDb * 1.5f);
  gain.SetDestGain(-2 * Gain::kMaxGainDb);
  EXPECT_EQ(kScale24DbDown, gain.GetGainScale());
  EXPECT_FALSE(gain.IsUnity());
  EXPECT_FALSE(gain.IsSilent());

  // AudioCore limits master to 0dB, but Gain object handles up to kMaxGainDb.
  // Dest also clamps to +24dB: source(-48dB) + dest(+36dB=>24dB) becomes -24dB.
  gain.SetSourceGain(-2 * Gain::kMaxGainDb);
  gain.SetDestGain(Gain::kMaxGainDb * 1.5f);
  EXPECT_EQ(kScale24DbDown, gain.GetGainScale());
  EXPECT_FALSE(gain.IsUnity());
  EXPECT_FALSE(gain.IsSilent());
}

void TestMinMuteGain(float source_gain_db, float dest_gain_db) {
  Gain gain;

  gain.SetSourceGain(source_gain_db);
  EXPECT_EQ(0.0f, gain.GetGainScale(dest_gain_db));

  gain.SetDestGain(dest_gain_db);
  EXPECT_EQ(0.0f, gain.GetGainScale());
  EXPECT_TRUE(gain.IsSilent());
  EXPECT_FALSE(gain.IsUnity());
}

// System independently limits AudioRenderer and Output Gains to kMinGainDb
// (-160dB). Assert scale is zero, if either (or combo) are kMinGainDb or less.
TEST(Gain, MinMute) {
  // if OutputGain <= kMinGainDb, scale must be 0, regardless of renderer gain.
  TestMinMuteGain(-2 * Gain::kMinGainDb, Gain::kMinGainDb);

  // if renderer gain <= kMinGainDb, scale must be 0, regardless of Output gain.
  TestMinMuteGain(Gain::kMinGainDb, Gain::kMaxGainDb * 1.2);

  // if sum of renderer gain and Output gain <= kMinGainDb, scale should be 0.
  // Output gain is just slightly above MinGain; renderer takes us below it.
  TestMinMuteGain(-2.0f, Gain::kMinGainDb + 1.0f);
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
//
// The 'MixGain' tests involve gain-scaling in the context of mixing (as opposed
// to earlier tests that probe the Gain object in isolation).

// Verify whether per-stream gain interacts linearly with accumulation buffer.
TEST(MixGain, Scaling_Linearity) {
  int16_t source[] = {0x0CE4, 0x0CCC, 0x23, 4, -0x0E, -0x19, -0x0CCC, -0x0CDB};
  float accum[8];
  Gain gain;

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  float stream_gain_db = 20.0f;

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               44100, 1, 44100, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_gain_db);

  float expect[] = {0x080E8000,  0x07FF8000,  0x015E000,   0x00028000,
                    -0x0008C000, -0x000FA000, -0x07FF8000, -0x0808E000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -12.0411998 dB leads to exactly 0.25x in value
  stream_gain_db = -12.0411998f;

  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1,
                      44100, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        stream_gain_db);

  float expect2[] = {0x00339000,  0x00333000,  0x00008C00,  0x00001000,
                     -0x00003800, -0x00006400, -0x00333000, -0x00336C00};
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our gain scaling respond to scale values close to the limits?
// Using 16-bit inputs, verify the behavior of our Gain object when given the
// closest-to-Unity and closest-to-Mute scale values.
TEST(MixGain, Scaling_Precision) {
  int16_t max_source[] = {0x7FFF, -0x8000};  // max/min 16-bit signed values.
  float accum[2];

  // kMinGainDbUnity is the lowest (furthest-from-Unity) with no observable
  // attenuation on full-scale (i.e. the smallest indistinguishable from Unity).
  // At this gain_scale, audio should be unchanged.
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), max_source, accum, false, fbl::count_of(accum),
        AudioResult::kMinGainDbUnity);

  //  At this gain_scale, resulting audio should be unchanged.
  float max_expect1[] = {0x07FFF000, -0x08000000};  // left-shift source by 12.
  NormalizeInt28ToPipelineBitwidth(max_expect1, fbl::count_of(max_expect1));
  EXPECT_TRUE(CompareBuffers(accum, max_expect1, fbl::count_of(accum)));

  // This is the highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), max_source, accum, false, fbl::count_of(accum),
        AudioResult::kMaxGainDbNonUnity);

  // Float32 has 25-bit precision (not 28), hence our min delta is 8 (not 1).
  float max_expect2[] = {0x07FFEFF8, -0x07FFFFF8};
  NormalizeInt28ToPipelineBitwidth(max_expect2, fbl::count_of(max_expect2));
  EXPECT_TRUE(CompareBuffers(accum, max_expect2, fbl::count_of(accum)));

  // kMinGainDbNonMute is the lowest (closest-to-zero) at which audio is not
  // silenced (i.e. the smallest that is distinguishable from Mute).  Although
  // the results may be smaller than we can represent in our 28-bit test data
  // representation, they are still non-zero and thus validate our scalar limit.
  int16_t min_source[] = {1, -1};
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), min_source, accum, false, fbl::count_of(accum),
        AudioResult::kMinGainDbNonMute);

  // The method used elsewhere in this file for expected result arrays (28-bit
  // fixed-point, normalized into float) cannot precisely express these values.
  // Nonetheless, they are present and non-zero!
  float min_expect[] = {3.051763215e-13, -3.051763215e-13};
  EXPECT_TRUE(CompareBuffers(accum, min_expect, fbl::count_of(accum)));

  //
  // kMaxGainDbMute is the highest (furthest-from-Mute) scalar that silences
  // full scale data (i.e. the largest AScale that is indistinguishable from
  // Mute). Consider an AScale value corresponding to ever-so-slightly above
  // -160dB: if this increment is small enough, the float32 cannot discern it
  // and treats it as -160dB, our limit for "automatically mute".  Per a mixer
  // optimization, if gain is Mute-equivalent, we skip mixing altogether. This
  // is equivalent to setting 'accumulate' and adding zeroes, so set that flag
  // here and expect no change in the accumulator, even with max inputs.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), max_source, accum, true, fbl::count_of(accum),
        AudioResult::kMaxGainDbMute);

  EXPECT_TRUE(CompareBuffers(accum, min_expect, fbl::count_of(accum)));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(MixGain, Accumulator) {
  int16_t source[] = {0x7FFF, -0x8000};
  float accum[] = {0x07FFF000, -0x08000000};
  float expect[] = {0x0FFFE000, -0x10000000};
  float expect2[] = {0x17FFD000, -0x18000000};

  // When mixed, these far exceed any int16 range
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  // These values exceed the per-stream range of int16
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // these values even exceed uint16
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, 1);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Our mixer contains an optimization in which it skips mixing operations if it
// detects that gain is below a certain threshold (regardless of "accumulate").
TEST(MixGain, Accumulator_Clear) {
  int16_t source[] = {-32768, 32767};
  float accum[] = {-32768, 32767};
  float expect[] = {-32768, 32767};

  // We will test both SampleAndHold and LinearInterpolation interpolators.
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  // Use the gain guaranteed to silence all signals: Gain::kMinScale.
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        Gain::kMinGainDb);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // Try with the other sampler.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::LinearInterpolation);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum),
        Gain::kMinGainDb);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // When accumulate = false, this is overridden: it behaves identically.
  //
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        Gain::kMinGainDb);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // Ensure that both samplers behave identically in this regard.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::LinearInterpolation);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum),
        Gain::kMinGainDb);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Headroom - post-SUM gain
// TODO(mpuryear): When we have a master gain stage that can take advantage of
// the headroom inherent in a multi-stream accumulator, implement this test.

}  // namespace test
}  // namespace audio
}  // namespace media
