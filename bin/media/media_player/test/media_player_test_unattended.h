// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_MEDIA_PLAYER_TEST_UNATTENDED_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_MEDIA_PLAYER_TEST_UNATTENDED_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "garnet/bin/media/media_player/test/fakes/fake_audio_renderer.h"
#include "garnet/bin/media/media_player/test/fakes/fake_wav_reader.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {
namespace test {

class MediaPlayerTestUnattended {
 public:
  MediaPlayerTestUnattended(fit::function<void(int)> quit_callback);

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  fit::function<void(int)> quit_callback_;
  FakeWavReader fake_reader_;
  FakeAudioRenderer fake_audio_renderer_;
  fuchsia::mediaplayer::MediaPlayerPtr media_player_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_MEDIA_PLAYER_TEST_UNATTENDED_H_
