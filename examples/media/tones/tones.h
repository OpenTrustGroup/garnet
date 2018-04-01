// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vmo_mapper.h>
#include <list>
#include <map>

#include "garnet/examples/media/tones/tone_generator.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/macros.h"
#include <fuchsia/cpp/media.h>

namespace examples {

class Tones {
 public:
  Tones(bool interactive);

  ~Tones();

 private:
  // Quits the app.
  void Quit();

  // Calls |HandleKeystroke| on the message loop when console input is ready.
  void WaitForKeystroke();

  // Handles a keystroke, possibly calling |WaitForKeystroke| to wait for the
  // next one.
  void HandleKeystroke();

  // Adds notes to the score.
  void BuildScore();

  // Start the Tone example.
  void Start();

  // Sends as much content as is currently demanded. Ends the stream when all
  // content has been sent.
  void Send(uint32_t amt);

  // Fills |buffer| with audio.
  void FillBuffer(float* buffer);

  // Determines whether all audio has been sent.
  bool done() const {
    return !interactive_ && frequencies_by_pts_.empty() &&
           tone_generators_.empty();
  }

  bool interactive_;
  fsl::FDWaiter fd_waiter_;
  media::AudioRenderer2Ptr audio_renderer_;
  std::map<int64_t, float> frequencies_by_pts_;
  std::list<ToneGenerator> tone_generators_;
  int64_t pts_ = 0;
  fbl::VmoMapper payload_buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Tones);
};

}  // namespace examples
