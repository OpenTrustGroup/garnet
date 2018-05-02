// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/ffmpeg/ffmpeg_init.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
}

namespace media_player {

void InitFfmpeg() {
  static bool initialized = []() {
    av_register_all();
    return true;
  }();

  (void)initialized;
}

}  // namespace media_player
