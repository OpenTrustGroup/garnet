// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include <fuchsia/cpp/media.h>

namespace media {
namespace audio {
namespace mixers {

class LinearSampler : public Mixer {
 public:
  static MixerPtr Select(const AudioMediaTypeDetails& src_format,
                         const AudioMediaTypeDetails& dst_format);

 protected:
  LinearSampler(uint32_t pos_filter_width, uint32_t neg_filter_width)
      : Mixer(pos_filter_width, neg_filter_width) {}
};

}  // namespace mixers
}  // namespace audio
}  // namespace media
