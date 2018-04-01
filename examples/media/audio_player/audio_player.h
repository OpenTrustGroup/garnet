// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/examples/media/audio_player/audio_player_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include <fuchsia/cpp/media.h>
#include "lib/media/timeline/timeline_function.h"

namespace examples {

class AudioPlayer {
 public:
  AudioPlayer(const AudioPlayerParams& params);

  ~AudioPlayer();

 private:
  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandleStatusUpdates(
      uint64_t version = media::kInitialStatus,
      media::MediaPlayerStatusPtr status = nullptr);

  media::MediaPlayerPtr media_player_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;
  bool quit_when_done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioPlayer);
};

}  // namespace examples
