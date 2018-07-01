// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "lib/app/cpp/startup_context.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  media::audio::AudioServerImpl impl;
  loop.Run();
  return 0;
}
