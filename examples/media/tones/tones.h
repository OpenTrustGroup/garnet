// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <map>

#include <fbl/vmo_mapper.h>
#include <fuchsia/cpp/media.h>

#include "garnet/examples/media/tones/tone_generator.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace examples {

class MidiKeyboard;

class Tones {
 public:
  Tones(bool interactive, fxl::Closure quit_callback);

  ~Tones();

 private:
  friend class MidiKeyboard;

  // Quits the app.
  void Quit();

  // Calls |HandleKeystroke| on the message loop when console input is ready.
  void WaitForKeystroke();

  // Handles a keystroke, possibly calling |WaitForKeystroke| to wait for the
  // next one.
  void HandleKeystroke();

  // Handles a note on/off event from a midi keyboard for the specified note.  0
  // corresponds to middle C, every tick above or below is a distance of 1/2 a
  // step.  So, -1 would be the B below middle C, while 3 would be the D# above
  // middle C.
  void HandleMidiNote(int note, int velocity, bool note_on);

  // Adds notes to the score.
  void BuildScore();

  // Start the Tone example.
  void Start(int64_t min_lead_time_nsec);

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
  fxl::Closure quit_callback_;
  fsl::FDWaiter fd_waiter_;
  media::AudioRenderer2Ptr audio_renderer_;
  std::map<int64_t, float> frequencies_by_pts_;
  std::list<ToneGenerator> tone_generators_;
  int64_t pts_ = 0;
  fbl::VmoMapper payload_buffer_;
  std::unique_ptr<MidiKeyboard> midi_keyboard_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Tones);
};

}  // namespace examples
