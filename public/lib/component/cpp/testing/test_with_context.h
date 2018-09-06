// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_TEST_WITH_CONTEXT_H_
#define LIB_APP_CPP_TESTING_TEST_WITH_CONTEXT_H_

#include "lib/component/cpp/testing/startup_context_for_test.h"
#include "lib/gtest/test_loop_fixture.h"

namespace component {
namespace testing {

// Test fixture for tests where a |StartupContext| is needed.
// Code under test can be given a context, while the test can use a |Controller|
// to set up and access the test environment.
class TestWithContext : public gtest::TestLoopFixture {
  using Controller = StartupContextForTest::Controller;

 protected:
  TestWithContext();
  std::unique_ptr<StartupContext> TakeContext();
  const Controller& controller() const { return *controller_; }

 private:
  std::unique_ptr<StartupContextForTest> context_;
  const Controller* controller_;
};

}  // namespace testing
}  // namespace component

#endif  // LIB_APP_CPP_TESTING_TEST_WITH_CONTEXT_H_
