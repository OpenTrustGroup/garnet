// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/files/unique_fd.h"

namespace examples {

class Tones;

class MidiKeyboard {
 public:
  // Attempt open and use the first MIDI event source we encounter.
  static std::unique_ptr<MidiKeyboard> Create(Tones* owner);

 private:
  friend std::unique_ptr<MidiKeyboard>::deleter_type;

  MidiKeyboard(Tones* owner, fxl::UniqueFD dev)
      : owner_(owner), dev_(std::move(dev)) {}
  ~MidiKeyboard();

  void Wait();
  void HandleEvent();

  Tones* const owner_;
  const fxl::UniqueFD dev_;
  fsl::FDWaiter fd_waiter_;
  bool waiting_ = false;
};

}  // namespace examples
