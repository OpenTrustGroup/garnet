// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "garnet/bin/media/audio_server/constants.h"
#include "lib/media/timeline/timeline.h"

namespace media {
namespace audio {

AudioRendererFormatInfo::AudioRendererFormatInfo(
    AudioMediaTypeDetails format)
    : format_(std::move(format)) {
  // Precompute some useful timing/format stuff.
  //
  // Start with the ratio between frames and nanoseconds.
  frames_per_ns_ =
      TimelineRate(format_.frames_per_second, Timeline::ns_from_seconds(1));

  // Figure out the rate we need to scale by in order to produce our fixed
  // point timestamps.
  frame_to_media_ratio_ = TimelineRate(1 << kPtsFractionalBits, 1);

  // Figure out the total number of bytes in a packed frame.
  switch (format_.sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      bytes_per_frame_ = 1;
      break;

    case AudioSampleFormat::SIGNED_16:
      bytes_per_frame_ = 2;
      break;

    case AudioSampleFormat::SIGNED_24_IN_32:
      bytes_per_frame_ = 4;
      break;

    default:
      // Format filtering was supposed to happen during
      // AudioRendererImpl::SetMediaType.  It should never be attempting to
      // create a FormatInfo structure with a sample format that we do not
      // understand.
      FXL_CHECK(false) << "unrecognized sample format";
      bytes_per_frame_ = 2;
      break;
  }

  bytes_per_frame_ *= format_.channels;
}

// static
fbl::RefPtr<AudioRendererFormatInfo> AudioRendererFormatInfo::Create(
    AudioMediaTypeDetails format) {
  return fbl::AdoptRef(new AudioRendererFormatInfo(std::move(format)));
}

}  // namespace audio
}  // namespace media
