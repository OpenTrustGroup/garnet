// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/gain.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

constexpr unsigned int Gain::kFractionalScaleBits;
constexpr Gain::AScale Gain::kUnityScale;
constexpr Gain::AScale Gain::kMaxScale;
constexpr float Gain::kMinGain;
constexpr float Gain::kMaxGain;

Gain::AScale Gain::GetGainScale(float output_db_gain) {
  float db_target_rend_gain = db_target_rend_gain_.load();

  // If nothing changed, return the previously-computed amplitude scale value.
  if ((db_current_rend_gain_ == db_target_rend_gain) &&
      (db_current_output_gain_ == output_db_gain)) {
    return amplitude_scale_;
  }

  // Update the internal gains, clamping in the process.
  db_current_rend_gain_ = fbl::clamp(db_target_rend_gain, kMinGain, kMaxGain);
  db_current_output_gain_ = fbl::clamp(output_db_gain, kMinGain, 0.0f);

  float effective_gain = db_current_rend_gain_ + db_current_output_gain_;

  // If either the renderer, output, or combined gain is at the force mute
  // point, just zero out the amplitude scale and return that.
  if ((db_current_rend_gain_ <= kMinGain) ||
      (db_current_output_gain_ <= kMinGain) || (effective_gain <= kMinGain)) {
    amplitude_scale_ = 0u;
  } else {
    // Compute the amplitude scale factor as a double and sanity check before
    // converting to the fixed point representation.
    double amp_scale = pow(10.0, effective_gain / 20.0);
    static_assert((kMaxScale) < (std::numeric_limits<uint32_t>::max()),
                  "kMaxScale exceeds the capacity of our AScale container!");

    amp_scale *= static_cast<double>(kUnityScale);
    // TODO(mpuryear): MTWN-73 round amp_scale instead of truncating
    amplitude_scale_ = static_cast<AScale>(amp_scale);
    FXL_DCHECK(amplitude_scale_ <= kMaxScale);
  }

  return amplitude_scale_;
}

}  // namespace audio
}  // namespace media
