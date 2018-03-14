// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/platform/generic/mixer.h"

#include "garnet/bin/media/audio_server/platform/generic/mixers/linear_sampler.h"
#include "garnet/bin/media/audio_server/platform/generic/mixers/no_op.h"
#include "garnet/bin/media/audio_server/platform/generic/mixers/point_sampler.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

constexpr uint32_t Mixer::FRAC_ONE;
constexpr uint32_t Mixer::FRAC_MASK;

Mixer::~Mixer() {}

Mixer::Mixer(uint32_t pos_filter_width, uint32_t neg_filter_width)
    : pos_filter_width_(pos_filter_width),
      neg_filter_width_(neg_filter_width) {}

MixerPtr Mixer::Select(const AudioMediaTypeDetailsPtr& src_format,
                       const AudioMediaTypeDetailsPtr* optional_dst_format) {
  // We should always have a source format.
  FXL_DCHECK(src_format);
  // We have no mixer for these formats.
  FXL_DCHECK(src_format->sample_format != AudioSampleFormat::ANY);
  FXL_DCHECK(src_format->sample_format != AudioSampleFormat::NONE);
  // MTWN-93: Consider eliminating these enums; they never lead to happy endings

  // If we don't have a destination format, just stick with no-op.  This is
  // probably the ThrottleOutput we are picking a mixer for.
  if (!optional_dst_format) {
    return MixerPtr(new mixers::NoOp());
  }

  const AudioMediaTypeDetailsPtr& dst_format = *optional_dst_format;
  FXL_DCHECK(dst_format);
  // MTWN-93: same as above, for these destination sample formats
  FXL_DCHECK(dst_format->sample_format != AudioSampleFormat::ANY);
  FXL_DCHECK(dst_format->sample_format != AudioSampleFormat::NONE);

  // If the source sample rate is an integer multiple of the destination sample
  // rate, just use the point sampler.  Otherwise, use the linear re-sampler.
  TimelineRate src_to_dst(src_format->frames_per_second,
                          dst_format->frames_per_second);
  if (src_to_dst.reference_delta() == 1) {
    return mixers::PointSampler::Select(src_format, dst_format);
  } else {
    return mixers::LinearSampler::Select(src_format, dst_format);
  }
}

}  // namespace audio
}  // namespace media
