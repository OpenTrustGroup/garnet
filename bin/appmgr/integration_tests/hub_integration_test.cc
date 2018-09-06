// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>

#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/glob.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace {

// This test fixture will provide a way to run components in provided launchers
// and check for errors.
class HubTest : public component::testing::TestWithEnvironment {
 protected:
  // This would launch component and check that it returns correct
  // |expected_return_code|.
  void RunComponent(const fuchsia::sys::LauncherPtr& launcher,
                    const std::string& component_url,
                    const std::vector<std::string>& args,
                    int64_t expected_return_code) {
    std::FILE* outf = std::tmpfile();
    int out_fd = fileno(outf);
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = component_url;
    for (auto arg : args) {
      launch_info.arguments.push_back(arg);
    }

    launch_info.out = component::testing::CloneFileDescriptor(out_fd);

    fuchsia::sys::ComponentControllerPtr controller;
    launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

    int64_t return_code = INT64_MIN;
    controller.events().OnTerminated =
        [&return_code](int64_t code,
                       fuchsia::sys::TerminationReason reason) {
          return_code = code;
        };
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&return_code] { return return_code != INT64_MIN; }, zx::sec(10)));
    std::string output;
    ASSERT_TRUE(files::ReadFileDescriptorToString(out_fd, &output));
    EXPECT_EQ(expected_return_code, return_code)
        << "failed for: " << fxl::JoinStrings(args, ", ")
        << "\noutput: " << output;
  }
};

TEST(ProbeHub, Component) {
  auto glob_str = fxl::StringPrintf("/hub/c/sysmgr/*/out/debug");
  files::Glob glob(glob_str);
  EXPECT_EQ(glob.size(), 1u) << glob_str << " expected to match once.";
}

TEST(ProbeHub, Realm) {
  auto glob_str = fxl::StringPrintf("/hub/r/sys/*/c/");
  files::Glob glob(glob_str);
  EXPECT_EQ(glob.size(), 1u) << glob_str << " expected to match once.";
}

TEST(ProbeHub, RealmSvc) {
  auto glob_str = fxl::StringPrintf("/hub/r/sys/*/svc/fuchsia.sys.Environment");
  files::Glob glob(glob_str);
  EXPECT_EQ(glob.size(), 1u);
}

TEST_F(HubTest, ScopePolicy) {
  std::string glob_url = "glob";
  // create nested environment
  // test that we can see nested env
  auto nested_env = CreateNewEnclosingEnvironment("hubscopepolicytest");
  ASSERT_TRUE(WaitForEnclosingEnvToStart(nested_env.get()));
  RunComponent(launcher_ptr(), glob_url, {"/hub/r/hubscopepolicytest/"}, 0);

  // test that we cannot see nested env using its own launcher
  RunComponent(nested_env->launcher_ptr(), glob_url,
               {"/hub/r/hubscopepolicytest"}, 1);

  // test that we can see check_hub_path
  RunComponent(nested_env->launcher_ptr(), glob_url, {"/hub/c/glob"}, 0);
}

TEST_F(HubTest, ThreadFiles) {
  std::string glob_url = "glob";

  auto nested_env = CreateNewEnclosingEnvironment("hubscopepolicytest");
  ASSERT_TRUE(WaitForEnclosingEnvToStart(nested_env.get()));
  RunComponent(launcher_ptr(), glob_url, {"/hub/r/hubscopepolicytest/"}, 0);

  // test that we can see threads for the new component
  RunComponent(nested_env->launcher_ptr(), glob_url,
               {"/hub/c/glob/*/system_objects/threads/*"}, 0);

  // Get the threads from all components in the sys realm, ensure their thread
  // files have arch specified.
  files::Glob glob("/hub/c/sysmgr/*/system_objects/threads/*");
  EXPECT_NE(glob.size(), 0u);
  for (const auto& f : glob) {
    std::string out;
    EXPECT_TRUE(files::ReadFileToString(fxl::StringPrintf("%s/.data", f), &out))
        << "failed to read file " << f;
    EXPECT_TRUE(out.find("arch:") != std::string::npos);
  }
}

}  // namespace
}  // namespace component
