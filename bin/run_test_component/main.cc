// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/termination_reason.h>
#include <lib/fdio/util.h>

#include "garnet/bin/run_test_component/env_config.h"
#include "garnet/bin/run_test_component/run_test_component.h"
#include "garnet/bin/run_test_component/test_metadata.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/component/cpp/testing/enclosing_environment.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/glob.h"
#include "lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;

namespace {
constexpr char kEnv[] = "env_for_test";
constexpr char kConfigPath[] =
    "/system/data/run_test_component/environment.config";

void PrintUsage() {
  fprintf(stderr, R"(
Usage: run_test_component <test_url> [arguments...]
       run_test_component <test_prefix> [arguments...]

       *test_url* takes the form of component manifest URL which uniquely
       identifies a test component. Example:
          fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx

       if *test_prefix* is provided, this tool will glob over /pkgfs and look for
       matching cmx files. If multiple files are found, it will print
       corresponding component URLs and exit.  If there is only one match, it
       will generate a component URL and execute the test.

       example:
        run_test_component run_test_component_unit
          will match /pkgfs/packages/run_test_component_unittests/meta/run_test_component_unittests.cmx and run it.
)");
}

bool ConnectToRequiredEnvironment(const run::EnvironmentType& env_type,
                                  zx::channel request) {
  std::string current_env;
  files::ReadFileToString("/hub/name", &current_env);
  std::string svc_path = "/hub/svc";
  switch (env_type) {
    case run::EnvironmentType::ROOT:
      if (current_env != "app") {
        fprintf(stderr,
                "Cannot run test in root environment as this utility was "
                "started in '%s' environment",
                current_env.c_str());
        return false;
      }
      break;
    case run::EnvironmentType::SYS:
      if (current_env == "app") {
        files::Glob glob("/hub/r/sys/*/svc");
        if (glob.size() != 1) {
          fprintf(stderr, "Cannot run test. Something wrong with hub.");
          return false;
        }
        svc_path = *(glob.begin());
      } else if (current_env != "sys") {
        fprintf(stderr,
                "Cannot run test in sys environment as this utility was "
                "started in '%s' environment",
                current_env.c_str());
        return false;
      }
      break;
  }

  // launch test
  zx::channel h1, h2;
  zx_status_t status;
  if ((status = zx::channel::create(0, &h1, &h2)) != ZX_OK) {
    fprintf(stderr, "Cannot create channel, status: %d", status);
    return false;
  }
  if ((status = fdio_service_connect(svc_path.c_str(), h1.release())) !=
      ZX_OK) {
    fprintf(stderr, "Cannot connect to %s, status: %d", svc_path.c_str(),
            status);
    return false;
  }

  if ((status = fdio_service_connect_at(
           h2.get(), fuchsia::sys::Environment::Name_, request.release())) !=
      ZX_OK) {
    fprintf(stderr, "Cannot connect to env service, status: %d", status);
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, const char** argv) {
  run::EnvironmentConfig config;
  config.ParseFromFile(kConfigPath);
  if (config.HasError()) {
    fprintf(stderr, "Error parsing config file %s: %s\n", kConfigPath,
            config.error_str().c_str());
    return 1;
  }

  auto parse_result = run::ParseArgs(argc, argv, "/pkgfs/packages");
  if (parse_result.error) {
    if (parse_result.error_msg != "") {
      fprintf(stderr, "%s\n", parse_result.error_msg.c_str());
    }
    PrintUsage();
    return 1;
  }
  if (parse_result.matching_urls.size() > 1) {
    fprintf(stderr, "Found multiple matching components. Did you mean?\n");
    for (auto url : parse_result.matching_urls) {
      fprintf(stderr, "%s\n", url.c_str());
    }
    return 1;
  } else if (parse_result.matching_urls.size() == 1) {
    fprintf(stdout, "Found one matching component. Running: %s\n",
            parse_result.matching_urls[0].c_str());
  }
  std::string program_name = parse_result.launch_info.url;
  run::TestMetadata test_metadata;
  if (!test_metadata.ParseFromFile(parse_result.cmx_file_path)) {
    fprintf(stderr, "Error parsing cmx %s: %s\n",
            parse_result.cmx_file_path.c_str(),
            test_metadata.error_str().c_str());
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();

  fuchsia::sys::ComponentControllerPtr controller;
  fuchsia::sys::EnvironmentPtr parent_env;
  fuchsia::sys::LauncherPtr launcher;
  std::unique_ptr<component::testing::EnclosingEnvironment> enclosing_env;

  auto map_entry = config.url_map().find(parse_result.launch_info.url);
  if (map_entry != config.url_map().end()) {
    if (test_metadata.HasServices()) {
      fprintf(stderr,
              "Cannot run this test in sys/root environment as it defines "
              "services in its '%s' facets\n",
              run::kFuchsiaTest);
      return 1;
    }
    if (!ConnectToRequiredEnvironment(map_entry->second,
                                      parent_env.NewRequest().TakeChannel())) {
      return 1;
    }
    parse_result.launch_info.out =
        component::testing::CloneFileDescriptor(STDOUT_FILENO);
    parse_result.launch_info.err =
        component::testing::CloneFileDescriptor(STDERR_FILENO);
    parent_env->GetLauncher(launcher.NewRequest());
  } else {
    context->ConnectToEnvironmentService(parent_env.NewRequest());
    enclosing_env =
        component::testing::EnclosingEnvironment::Create(kEnv, parent_env);
    auto services = test_metadata.TakeServices();
    for (auto& service : services) {
      enclosing_env->AddServiceWithLaunchInfo(std::move(service.second),
                                              service.first);
    }
    launcher = enclosing_env->launcher_ptr();
  }

  launcher->CreateComponent(std::move(parse_result.launch_info),
                            controller.NewRequest());

  controller.events().OnTerminated = [&program_name](
                                         int64_t return_code,
                                         TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", program_name.c_str(),
              component::HumanReadableTerminationReason(termination_reason)
                  .c_str());
    }
    zx_process_exit(return_code);
  };

  loop.Run();
  return 0;
}
