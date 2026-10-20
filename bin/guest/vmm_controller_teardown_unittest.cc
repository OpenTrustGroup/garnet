// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/vmm_controller_teardown.h"

#include <gtest/gtest.h>

namespace {

TEST(VmmControllerTeardownLifecycleTest, FirstTeardownRequestWins) {
  VmmTeardownLifecycle lifecycle;

  EXPECT_TRUE(lifecycle.RequestTeardown());
  EXPECT_FALSE(lifecycle.RequestTeardown());

  EXPECT_EQ(VmmTeardownLifecycle::State::kScheduled, lifecycle.state());
}

TEST(VmmControllerTeardownLifecycleTest,
     LifecycleCloseDuringTeardownCompletesAfterFinishOnly) {
  VmmTeardownLifecycle lifecycle;

  ASSERT_TRUE(lifecycle.RequestTeardown());
  EXPECT_TRUE(lifecycle.RecordLifecycleClose());
  EXPECT_FALSE(lifecycle.CompleteComponentStop());

  ASSERT_TRUE(lifecycle.BeginTeardown());
  EXPECT_FALSE(lifecycle.CompleteComponentStop());

  ASSERT_TRUE(lifecycle.FinishTeardown());
  EXPECT_TRUE(lifecycle.CompleteComponentStop());
  EXPECT_TRUE(lifecycle.component_stop_completed());
  EXPECT_FALSE(lifecycle.CompleteComponentStop());
}

TEST(VmmControllerTeardownLifecycleTest,
     LifecycleCloseAfterFinishedCompletesStopOnce) {
  VmmTeardownLifecycle lifecycle;

  ASSERT_TRUE(lifecycle.RequestTeardown());
  ASSERT_TRUE(lifecycle.BeginTeardown());
  ASSERT_TRUE(lifecycle.FinishTeardown());

  EXPECT_TRUE(lifecycle.RecordLifecycleClose());
  EXPECT_TRUE(lifecycle.CompleteComponentStop());
  EXPECT_FALSE(lifecycle.RecordLifecycleClose());
  EXPECT_FALSE(lifecycle.CompleteComponentStop());
}

TEST(VmmControllerTeardownLifecycleTest,
     ScheduledTeardownCanRunInlineAfterPostFailure) {
  VmmTeardownLifecycle lifecycle;

  ASSERT_TRUE(lifecycle.RequestTeardown());
  EXPECT_TRUE(lifecycle.teardown_active());

  EXPECT_TRUE(lifecycle.BeginTeardown());
  EXPECT_TRUE(lifecycle.teardown_active());

  EXPECT_TRUE(lifecycle.FinishTeardown());
  EXPECT_FALSE(lifecycle.teardown_active());
}

}  // namespace
