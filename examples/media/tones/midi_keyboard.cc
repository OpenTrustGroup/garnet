// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/tones/midi_keyboard.h"

#include <iostream>

#include <dirent.h>
#include <fcntl.h>
#include <lib/fit/defer.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/device/midi.h>

#include "garnet/examples/media/tones/midi.h"
#include "garnet/examples/media/tones/tones.h"
#include "lib/fxl/logging.h"

namespace examples {

static const char* const kDevMidiPath = "/dev/class/midi";

std::unique_ptr<MidiKeyboard> MidiKeyboard::Create(Tones* owner) {
  struct dirent* de;
  DIR* dir = opendir(kDevMidiPath);
  if (!dir) {
    if (errno != ENOENT) {
      FXL_LOG(WARNING) << "Error attempting to open \"" << kDevMidiPath
                       << "\" (errno " << errno << ")";
    }
    return nullptr;
  }

  auto cleanup = fit::defer([dir]() { closedir(dir); });

  while ((de = readdir(dir)) != NULL) {
    char devname[128];

    snprintf(devname, sizeof(devname), "%s/%s", kDevMidiPath, de->d_name);
    fxl::UniqueFD dev(open(devname, O_RDWR | O_NONBLOCK));
    if (!dev.is_valid()) {
      continue;
    }

    int device_type;
    int ret = ioctl_midi_get_device_type(dev.get(), &device_type);
    if (ret != sizeof(device_type)) {
      FXL_LOG(WARNING) << "ioctl_midi_get_device_type failed for \"" << devname
                       << "\"";
      continue;
    }

    if (device_type == MIDI_TYPE_SOURCE) {
      std::cout << "Creating MIDI source @ \"" << devname << "\"\n";
      std::unique_ptr<MidiKeyboard> ret(
          new MidiKeyboard(owner, std::move(dev)));
      ret->Wait();
      return ret;
    }
  }

  return nullptr;
}

MidiKeyboard::~MidiKeyboard() {
  if (waiting_) {
    fd_waiter_.Cancel();
  }
}

void MidiKeyboard::Wait() {
  fd_waiter_.Wait(
      [this](zx_status_t status, uint32_t events) { HandleEvent(); },
      dev_.get(), POLLIN);
  waiting_ = true;
}

void MidiKeyboard::HandleEvent() {
  waiting_ = false;

  while (true) {
    uint8_t event[3] = {0};
    int evt_size = ::read(dev_.get(), event, sizeof(event));

    if (evt_size < 0) {
      if (errno == EWOULDBLOCK) {
        break;
      }

      FXL_LOG(WARNING) << "Shutting down MIDI keyboard (errno " << errno << ")";
      return;
    }

    if (evt_size == 0) {
      break;
    }

    if (evt_size > 3) {
      FXL_LOG(WARNING) << "Shutting down MIDI keyboard, bad event size ("
                       << evt_size << ")";
      return;
    }

    // In theory, USB MIDI event sizes are always supposed to be 4 bytes.  1
    // byte for virtual MIDI cable IDs, and then 3 bytes of the MIDI event
    // padded using 0s to normalize the size.  The Fuchisa USB MIDI driver is
    // currently stripping the first byte and passing all virtual cable events
    // along as the same, but the subsequent bytes may or may not be there.
    //
    // For now, we fill our RX buffers with zero before reading, and attempt
    // to be handle the events in that framework.  Specifially, NOTE_ON events
    // with a 7-bit velocity value of 0 are supposed to be treated as NOTE_OFF
    // values.
    uint8_t cmd = event[0] & MIDI_COMMAND_MASK;
    if ((cmd == MIDI_NOTE_ON) || (cmd == MIDI_NOTE_OFF)) {
      // By default, MIDI event sources map the value 60 to middle C.
      static constexpr int kOffsetMiddleC = 60;
      int note =
          static_cast<int>(event[1] & MIDI_NOTE_NUMBER_MASK) - kOffsetMiddleC;
      int velocity = static_cast<int>(event[2] & MIDI_NOTE_VELOCITY_MASK);
      bool note_on = (cmd == MIDI_NOTE_ON) && (velocity != 0);

      owner_->HandleMidiNote(note, velocity, note_on);
    }
  }

  Wait();
}

}  // namespace examples
