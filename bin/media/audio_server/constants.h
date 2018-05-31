// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace media {
namespace audio {

// The number of fractional bits used when expressing timestamps (in frame
// units) as fixed point integers.  Ultimately, this determines the resolution
// that a source of PCM frames may be sampled at; there are 2^frac_bits
// positions between audio frames that the source stream may be sampled at.
//
// Using 64-bit signed timestamps means that we have 50 bits of whole frame
// units to work with.  At 192KHz, this allows for ~186.3 years of usable range
// before rollover when starting from a frame counter of 0.
//
// With 13 bits of fractional position, a mix job's interpolation precision is
// only +/-61 ppm. Across multiple jobs we stay in sync, but for any single mix,
// this is our granularity. As an example, when resampling a 48 kHz audio
// packet, the "clicks on the dial" of our actual resampling rates are multiples
// of 6 Hz. Again, we do correct any positional error at mix job boundaries.
//
// This also affects our interpolation accuracy: because fractional position has
// a potential error of 2^-13, the worst-case error for interpolated values is
// [pos_err * max_intersample_delta]. This means full-scale very high-frequency
// signals are only guaranteed bit-for-bit accurate in the top 13 bits.
// TODO(mpuryear): MTWN-86 Consider even more fractional position precision.
constexpr uint32_t kPtsFractionalBits = 13;
constexpr uint32_t kPtsRoundingVal = 1 << (kPtsFractionalBits - 1);
// Used in places where PTS must be an integral number of frames.
constexpr uint32_t kPtsFractionalMask = (1 << kPtsFractionalBits) - 1;

// This is the width of our signed fixed-point audio processing pipeline.
// Currently equivalent to "int18": uint8 and int16 are biased up by a few bits.
constexpr uint32_t kAudioPipelineWidth = 18;

// A compile time constant which is guaranteed to never be used as a valid
// generation ID (by any of the various things which use generation IDs to track
// changes to state).
constexpr uint32_t kInvalidGenerationId = 0;

}  // namespace audio
}  // namespace media
