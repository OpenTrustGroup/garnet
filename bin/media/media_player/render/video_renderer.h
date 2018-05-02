// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_VIDEO_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_VIDEO_RENDERER_H_

#include <fuchsia/cpp/geometry.h>

#include "garnet/bin/media/media_player/render/renderer.h"

namespace media_player {

// Abstract base class for sinks that render packets.
// TODO(dalesat): Rename this.
class VideoRendererInProc : public Renderer {
 public:
  VideoRendererInProc() {}

  ~VideoRendererInProc() override {}

  // Returns the current size of the video in pixels.
  virtual geometry::Size video_size() const = 0;

  // Returns the current pixel aspect ratio of the video.
  virtual geometry::Size pixel_aspect_ratio() const = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_VIDEO_RENDERER_H_
