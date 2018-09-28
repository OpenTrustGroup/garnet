// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <lib/fdio/spawn.h>
#include <lib/fxl/log_settings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>
#include <zircon/types.h>
#include <zircon/status.h>

#include "garnet/bin/trace/tests/run_test.h"

// The path to the test subprogram.
// This path can only be interpreted within the context of the test package.
#define TEST_APP_PATH "/pkg/bin/integration_test_app"

// The path of the trace program.
const char kTraceProgramPath[] = "/system/bin/trace";

void AppendLoggingArgs(std::vector<std::string>* argv, const char* prefix) {
  // Transfer our log settings to the subprogram.
  auto log_settings = fxl::GetLogSettings();
  std::string log_file_arg;
  std::string verbose_or_quiet_arg;
  if (log_settings.log_file != "") {
    log_file_arg = fxl::StringPrintf("%s--log-file=%s", prefix,
                                     log_settings.log_file.c_str());
    argv->push_back(log_file_arg);
  }
  if (log_settings.min_log_level != 0) {
    if (log_settings.min_log_level < 0) {
      verbose_or_quiet_arg = fxl::StringPrintf("%s--verbose=%d", prefix,
                                               -log_settings.min_log_level);
    } else {
      verbose_or_quiet_arg = fxl::StringPrintf("%s--quiet=%d", prefix,
                                               log_settings.min_log_level);
    }
    argv->push_back(verbose_or_quiet_arg);
  }
}

static void StringArgvToCArgv(const std::vector<std::string>& argv,
                              std::vector<const char*>* c_argv) {
  for (const auto& arg : argv) {
    c_argv->push_back(arg.c_str());
  }
  c_argv->push_back(nullptr);
}

static void BuildTraceProgramArgv(const std::string& tspec_path,
                                  const std::string& output_file_path,
                                  std::vector<std::string>* argv) {
  argv->push_back(kTraceProgramPath);
  AppendLoggingArgs(argv, "");
  argv->push_back("record");
  argv->push_back(fxl::StringPrintf("--spec-file=%s", tspec_path.c_str()));
  argv->push_back(fxl::StringPrintf("--output-file=%s",
                                    output_file_path.c_str()));

  AppendLoggingArgs(argv, "--append-args=");

  // Note that |tspec_path| cannot have a comma.
  argv->push_back(fxl::StringPrintf("--append-args=run,%s",
                                    tspec_path.c_str()));
}

static void BuildVerificationProgramArgv(const std::string& tspec_path,
                                         const std::string& output_file_path,
                                         std::vector<std::string>* argv) {
  argv->push_back(TEST_APP_PATH);

  AppendLoggingArgs(argv, "");

  argv->push_back("verify");
  argv->push_back(tspec_path.c_str());
  argv->push_back(output_file_path.c_str());
}

zx_status_t SpawnProgram(const zx::job& job,
                         const std::vector<std::string>& argv,
                         zx_handle_t arg_handle,
                         zx::process* out_process) {
  std::vector<const char*> c_argv;
  StringArgvToCArgv(argv, &c_argv);

  FXL_VLOG(1) << "Running " << fxl::JoinStrings(argv, " ");

  size_t action_count = 0;
  fdio_spawn_action_t spawn_actions[1];
  if (arg_handle != ZX_HANDLE_INVALID) {
    spawn_actions[0].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    spawn_actions[0].h.id = PA_HND(PA_USER0, 0);
    spawn_actions[0].h.handle = arg_handle;
    action_count = 1;
  }

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  auto status = fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL,
                               c_argv[0], c_argv.data(), nullptr,
                               action_count, &spawn_actions[0],
                               out_process->reset_and_get_address(),
                               err_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Spawning " << c_argv[0] << " failed: " << err_msg;
    return status;
  }

  return ZX_OK;
}

zx_status_t WaitAndGetExitCode(const std::string& program_name,
                               const zx::process& process,
                               int* out_exit_code) {
  auto status = process.wait_one(
    ZX_PROCESS_TERMINATED, zx::deadline_after(zx::duration(kTestTimeout)),
    nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed waiting for program " << program_name
                   << " to exit: " << zx_status_get_string(status);
    return status;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process.get(), ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting return code for program "
                   << program_name << ": " << zx_status_get_string(status);
    return status;
  }

  if (proc_info.return_code != 0) {
    FXL_LOG(ERROR) << program_name << " exited with exit code "
                   << proc_info.return_code;
  }
  *out_exit_code = proc_info.return_code;
  return ZX_OK;
}

// |verify=false| -> run the test
// |verify=true| -> verify the test
static bool RunTspecWorker(const std::string& tspec_path,
                           const std::string& output_file_path,
                           bool verify) {
  const char* operation_name = verify ? "Verifying" : "Running";
  FXL_LOG(INFO) << operation_name << " tspec " << tspec_path
                << ", output file " << output_file_path;
  zx::job job{}; // -> default job
  zx::process subprocess;

  std::vector<std::string> argv;
  if (!verify) {
    BuildTraceProgramArgv(tspec_path, output_file_path, &argv);
  } else {
    BuildVerificationProgramArgv(tspec_path, output_file_path, &argv);
  }

  auto status = SpawnProgram(job, argv, ZX_HANDLE_INVALID, &subprocess);
  if (status != ZX_OK) {
    return false;
  }

  int exit_code;
  status = WaitAndGetExitCode(argv[0], subprocess, 
                              &exit_code);
  if (status != ZX_OK) {
    return false;
  }
  if (exit_code != 0) {
    return false;
  }

  FXL_VLOG(1) << operation_name << " completed OK";
  return true;
}

bool RunTspec(const std::string& tspec_path,
              const std::string& output_file_path) {
  return RunTspecWorker(tspec_path, output_file_path, false /*run*/);
}

bool VerifyTspec(const std::string& tspec_path,
                 const std::string& output_file_path) {
  return RunTspecWorker(tspec_path, output_file_path, true /*verify*/);
}
