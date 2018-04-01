// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "gtest/gtest.h"

#include <lib/async/cpp/task.h>
#include <zx/time.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace testing {

// Note: GTest uses the top-level "testing" namespace while the Bluetooth test
// utilities are in "::btlib::testing".
class TestBase : public ::testing::Test {
 public:
  TestBase() = default;
  virtual ~TestBase() = default;

 protected:
  // Pure-virtual overrides of ::testing::Test::SetUp() to force subclass
  // override.
  void SetUp() override = 0;

  // ::testing::Test override:
  void TearDown() override {}

  // Posts a delayed task to quit the message loop after |time_delta| has
  // elapsed.
  void PostDelayedQuitTask(const fxl::TimeDelta& time_delta) {
    async::PostDelayedTask(message_loop_.async(),
                           [this] { message_loop_.QuitNow(); },
                           zx::nsec(time_delta.ToNanoseconds()));
  }

  // Runs the message loop for the specified amount of time. This is useful for
  // callback-driven test cases in which the message loop may run forever if the
  // callback is not run.
  void RunMessageLoop(int64_t timeout_seconds = 10) {
    RunMessageLoop(fxl::TimeDelta::FromSeconds(timeout_seconds));
  }

  void RunMessageLoop(const fxl::TimeDelta& time_delta) {
    PostDelayedQuitTask(time_delta);
    message_loop_.Run();
  }

  // Runs the message loop until it would wait.
  void RunUntilIdle() { message_loop_.RunUntilIdle(); }

  // Getters for internal fields frequently used by tests.
  fsl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  fsl::MessageLoop message_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestBase);
};

}  // namespace testing
}  // namespace btlib
