// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <iostream>

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>

#include "garnet/bin/trace/tests/run_test.h"

// Persistent storage is used to assist debugging failures.
const char kOutputFilePath[] = "/data/test.trace";

const char kUsageString[] = {
  "Usage: run fuchsia-pkg://fuchsia.com/trace_tests#meta/run_integration_test.cmx\\n"
  "  [options] /pkg/data/test.tspec\n"
  "\n"
  "Options:\n"
  "  --quiet[=LEVEL]    set quietness level (opposite of verbose)\n"
  "  --verbose[=LEVEL]  set debug verbosity level\n"
  "  --log-file=FILE    write log output to FILE\n"
};

static void PrintUsageString() { std::cout << kUsageString << std::endl; }

int main(int argc, char *argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  auto args = cl.positional_args();

  if (args.size() != 1) {
    FXL_LOG(ERROR) << "Missing tspec file";
    return EXIT_FAILURE;
  }
  auto tspec_path = args[0];

  if (!RunTspec(tspec_path, kOutputFilePath)) {
    return EXIT_FAILURE;
  }
  if (!VerifyTspec(tspec_path, kOutputFilePath)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
