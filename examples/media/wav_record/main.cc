// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/command_line.h"

#include "garnet/examples/media/wav_record/wav_recorder.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  auto application_context =
      component::ApplicationContext::CreateFromStartupInfo();
  examples::WavRecorder wav_recorder(
      fxl::CommandLineFromArgcArgv(argc, argv),
      [&loop]() { async::PostTask(loop.async(), [&loop]() { loop.Quit(); }); });
  wav_recorder.Run(application_context.get());
  loop.Run();

  return 0;
}
