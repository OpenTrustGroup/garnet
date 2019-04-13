// Copyright 2018 Open Trust Group
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <sys/types.h>

#include <lib/async-loop/cpp/loop.h>
#include "garnet/bin/gzos/sysmgr/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

#if WITH_DYNAMIC_SERVICE
#include "garnet/bin/gzos/sysmgr/dynamic_service_app.h"
#endif

constexpr char kConfigDir[] = "/system/data/sysmgr/";

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  sysmgr::Config config;
  if (command_line.HasOption("config")) {
    std::string config_data;
    command_line.GetOptionValue("config", &config_data);
    config.ParseFromString(config_data, "command line");
  } else {
    config.ParseFromDirectory(kConfigDir);
  }

  if (config.HasError()) {
    FXL_LOG(ERROR) << "Parsing config failed:\n" << config.error_str();
    return ZX_ERR_INVALID_ARGS;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
#if WITH_DYNAMIC_SERVICE
  sysmgr::DynamicServiceApp app;
#else
  sysmgr::App app(std::move(config));
#endif

  loop.Run();
  return 0;
}