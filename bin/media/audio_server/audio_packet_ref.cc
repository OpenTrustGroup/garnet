// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_packet_ref.h"
#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

AudioPacketRef::AudioPacketRef(AudioServerImpl* server,
                               uint32_t frac_frame_len,
                               int64_t start_pts)
    : server_(server),
      frac_frame_len_(frac_frame_len),
      start_pts_(start_pts),
      end_pts_(start_pts + frac_frame_len) {
  FXL_DCHECK(server_);
}

void AudioPacketRef::fbl_recycle() {
  // If the packet is dying for the first time, and we successfully queue it for
  // cleanup, allow it to live on until the cleanup actually runs.  Otherwise
  // the object is at its end of life.
  if (!was_recycled_) {
    was_recycled_ = true;
    if (NeedsCleanup()) {
      FXL_DCHECK(server_);
      server_->SchedulePacketCleanup(fbl::unique_ptr<AudioPacketRef>(this));
      return;
    }
  }

  delete this;
}

}  // namespace audio
}  // namespace media
