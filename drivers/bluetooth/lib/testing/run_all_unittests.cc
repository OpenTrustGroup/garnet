// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <ddk/driver.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"

#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

BT_DECLARE_FAKE_DRIVER();

using ::btlib::common::LogSeverity;

namespace {

LogSeverity FxlLogToBtLogLevel(fxl::LogSeverity severity) {
  switch (severity) {
    case fxl::LOG_ERROR:
      return LogSeverity::ERROR;
    case fxl::LOG_WARNING:
      return LogSeverity::WARN;
    case fxl::LOG_INFO:
      return LogSeverity::INFO;
    case -1:
      return LogSeverity::TRACE;
    case -2:
      return LogSeverity::SPEW;
    default:
      break;
  }
  if (severity < 0) {
    return LogSeverity::SPEW;
  }
  return LogSeverity::ERROR;
}

}  // namespace

int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  fxl::LogSettings log_settings;
  log_settings.min_log_level = fxl::LOG_ERROR;
  if (!fxl::ParseLogSettings(cl, &log_settings)) {
    return EXIT_FAILURE;
  }

  // Set all library log messages to use printf instead ddk logging.
  btlib::common::UsePrintf(FxlLogToBtLogLevel(log_settings.min_log_level));

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
