// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/echo/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/examples/testing/test_with_environment/fake_echo.h"
#include "lib/component/cpp/testing/test_with_environment.h"

// This test file demostrates how to use |TestWithEnvironment|.

namespace echo2 {
namespace testing {
namespace {

using component::testing::EnclosingEnvironment;
using component::testing::EnvironmentServices;
using component::testing::TestWithEnvironment;
using fidl::examples::echo::EchoPtr;
using fuchsia::sys::LaunchInfo;

const char kEnvironment[] = "environment_test";
const auto kTimeout = zx::sec(5);
const char kFakeEchoUrl[] =
    "fuchsia-pkg://fuchsia.com/test_with_environment_example_test#meta/"
    "fake_echo_app.cmx";

class TestWithEnvironmentExampleTest : public TestWithEnvironment {
 protected:
  std::unique_ptr<EnclosingEnvironment> enclosing_environment_;
  fidl::StringPtr answer_ = "Goodbye World!";
};

// Demonstrates use adding fake service to EnclosingEnvironment.
TEST_F(TestWithEnvironmentExampleTest, AddFakeEchoAsService) {
  // Start enclosing environment with an injected service.
  std::unique_ptr<EnvironmentServices> services = CreateServices();
  FakeEcho fake_echo;
  services->AddService(fake_echo.GetHandler());
  enclosing_environment_ =
      CreateNewEnclosingEnvironment(kEnvironment, std::move(services));

  fidl::StringPtr message = "bogus";
  fake_echo.SetAnswer(answer_);
  EchoPtr echo_ptr;
  // You can also launch your component which connects to echo service using
  // enclosing_environment_.CreateComponent(..).
  enclosing_environment_->ConnectToService(echo_ptr.NewRequest());
  echo_ptr->EchoString("Hello World!",
                       [&](::fidl::StringPtr retval) { message = retval; });
  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return message == answer_; }, kTimeout));
}

// Demonstrates use adding fake service as component to EnclosingEnvironment.
// |enclosing_environment_| launches kFakeEchoUrl when anything tries to connect
// to echo service in |enclosing_environment_|;
TEST_F(TestWithEnvironmentExampleTest, AddFakeEchoAsServiceComponent) {
  // Start enclosing environment with an injected service served by a
  // component.
  std::unique_ptr<EnvironmentServices> services = CreateServices();
  LaunchInfo launch_info;
  launch_info.url = kFakeEchoUrl;
  launch_info.arguments.push_back(answer_);
  services->AddServiceWithLaunchInfo(std::move(launch_info), Echo::Name_);
  enclosing_environment_ =
      CreateNewEnclosingEnvironment(kEnvironment, std::move(services));

  fidl::StringPtr message = "bogus";
  EchoPtr echo_ptr;
  // You can also launch your component which connects to echo service using
  // enclosing_environment_.CreateComponent(..).
  enclosing_environment_->ConnectToService(echo_ptr.NewRequest());
  echo_ptr->EchoString("Hello World!",
                       [&](::fidl::StringPtr retval) { message = retval; });
  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return message == answer_; }, kTimeout));
}

}  // namespace
}  // namespace testing
}  // namespace echo2
