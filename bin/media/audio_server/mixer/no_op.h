// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/mixer/mixer.h"

namespace media {
namespace audio {
namespace mixer {

class NoOp : public Mixer {
 public:
  NoOp() : Mixer(0, 0) {}

  bool Mix(int32_t* dst, uint32_t dst_frames, uint32_t* dst_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t modulo, uint32_t denominator) override;
};

}  // namespace mixer
}  // namespace audio
}  // namespace media
