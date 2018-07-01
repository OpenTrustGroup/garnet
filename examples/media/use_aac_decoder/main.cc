// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>

#include "use_aac_decoder.h"
#include "use_h264_decoder.h"

#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/command_line.h>

void usage(const char* prog_name) {
  printf("usage: %s (--aac_adts|--h264) <input_file> [<output_file>]\n",
         prog_name);
}

int main(int argc, char* argv[]) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.options().size() != 1 ||
      command_line.positional_args().size() < 1 ||
      command_line.positional_args().size() > 2) {
    usage(command_line.argv0().c_str());
    return -1;
  }

  async::Loop main_loop(&kAsyncLoopConfigMakeDefault);
  main_loop.StartThread("FIDL_thread");

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context =
      fuchsia::sys::StartupContext::CreateFromStartupInfo();
  fuchsia::mediacodec::CodecFactoryPtr codec_factory =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>();

  std::string input_file = command_line.positional_args()[0];
  std::string output_file;
  if (command_line.positional_args().size() >= 2) {
    output_file = command_line.positional_args()[1];
  }

  uint8_t md[SHA256_DIGEST_LENGTH];

  if (command_line.HasOption("aac_adts")) {
    use_aac_decoder(std::move(codec_factory), input_file, output_file, md);
  } else if (command_line.HasOption("h264")) {
    use_h264_decoder(std::move(codec_factory), input_file, output_file, md);
  } else {
    usage(command_line.argv0().c_str());
    return -1;
  }

  printf(
      "The sha256 of the output data (including data format "
      "parameters) is:\n");
  for (uint8_t byte : md) {
    printf("%02x", byte);
  }
  printf("\n");

  main_loop.Quit();
  main_loop.JoinThreads();
  startup_context.reset();
  main_loop.Shutdown();

  return 0;
}
