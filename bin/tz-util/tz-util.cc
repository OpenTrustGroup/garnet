// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <zircon/syscalls.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include <time_zone/cpp/fidl.h>

static constexpr char kGetOffsetCmd[] = "get_offset_minutes";
static constexpr char kSetTimezoneIdCmd[] = "set_timezone_id";
static constexpr char kGetTimezoneIdCmd[] = "get_timezone_id";

class TzUtil {
 public:
  TzUtil() {
    component::ConnectToEnvironmentService(timezone_.NewRequest());
  }

  void Run(fxl::CommandLine command_line) {
    if (command_line.HasOption("help")) {
      Usage();
      return;
    }
    if (command_line.HasOption(kSetTimezoneIdCmd)) {
      std::string timezone_id;
      command_line.GetOptionValue(kSetTimezoneIdCmd, &timezone_id);
      if (!timezone_id.empty()) {
        bool status;
        if (!timezone_->SetTimezone(timezone_id, &status) || !status) {
          std::cerr << "ERROR: Unable to set ID." << std::endl;
          exit(1);
        }
        return;
      } else {
        Usage();
      }
      return;
    }
    if (command_line.HasOption(kGetTimezoneIdCmd)) {
      fidl::StringPtr timezone_id;
      if (timezone_->GetTimezoneId(&timezone_id)) {
        std::cout << timezone_id << std::endl;
      } else {
        std::cerr << "ERROR: Unable to get timezone ID." << std::endl;
      }
      return;
    }
    if (command_line.HasOption(kGetOffsetCmd)) {
      int32_t local_offset, dst_offset;
      zx_time_t milliseconds_since_epoch =
          zx_clock_get(ZX_CLOCK_UTC) / ZX_MSEC(1);
      if (timezone_->GetTimezoneOffsetMinutes(milliseconds_since_epoch,
                                              &local_offset, &dst_offset)) {
        std::cout << local_offset + dst_offset << std::endl;
      } else {
        std::cerr << "ERROR: Unable to get offset." << std::endl;
      }
      return;
    }

    // Default: no args.
    Usage();
  }

 private:
  static void Usage() {
    std::cout << "Usage: tz-util [--help|"
              << "--" << kSetTimezoneIdCmd << "=ID|"
              << "--" << kGetTimezoneIdCmd << "|"
              << "--" << kGetOffsetCmd << "]" << std::endl;
    std::cout << std::endl;
  }

  time_zone::TimezoneSyncPtr timezone_;
};

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }
  TzUtil app;
  app.Run(command_line);
  return 0;
}
