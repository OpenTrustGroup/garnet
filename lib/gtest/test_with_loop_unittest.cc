// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/gtest/test_with_loop.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

namespace gtest {
namespace {

class TestWithLoopTest : public TestWithLoop {};

TEST_F(TestWithLoopTest, DefaultDispatcherIsSet) {
  EXPECT_EQ(async_get_default(), dispatcher());
}

TEST_F(TestWithLoopTest, TimeIsAdvanced){
  EXPECT_EQ(Now(), zx::time(0));

  AdvanceTimeTo(zx::time(0) + zx::sec(5));
  EXPECT_EQ(Now(), zx::time(0) + zx::sec(5));

  AdvanceTimeBy(zx::sec(5));
  EXPECT_EQ(Now(), zx::time(0) + zx::sec(10));

  RunLoopUntil(zx::time(0) + zx::sec(15));
  EXPECT_EQ(Now(), zx::time(0) + zx::sec(15));

  RunLoopFor(zx::sec(5));
  EXPECT_EQ(Now(), zx::time(0) + zx::sec(20));

  // RunLoopUntilIdle() should not advance the time.
  RunLoopUntilIdle();
  EXPECT_EQ(Now(), zx::time(0) + zx::sec(20));
}

TEST_F(TestWithLoopTest, WorkBeingDoneIsReported) {
  EXPECT_FALSE(RunLoopUntilIdle());
  EXPECT_FALSE(RunLoopUntil(zx::time(0) + zx::sec(5)));
  EXPECT_FALSE(RunLoopFor(zx::sec(5)));

  async::PostTask(dispatcher(), [] {});
  EXPECT_TRUE(RunLoopUntilIdle());

  async::PostTaskForTime(dispatcher(), [] {}, zx::time(0) + zx::sec(15));
  EXPECT_TRUE(RunLoopUntil(zx::time(0) + zx::sec(15)));

  async::PostDelayedTask(dispatcher(), [] {}, zx::sec(5));
  EXPECT_TRUE(RunLoopFor(zx::sec(5)));
}

TEST_F(TestWithLoopTest, LoopCanQuitAndReset) {
  async::PostDelayedTask(dispatcher(), [] {}, zx::sec(1));
  QuitLoop();

  // Loop has quit, so time does not advance and no work is done.
  EXPECT_FALSE(RunLoopFor(zx::sec(1)));
  EXPECT_EQ(Now(), zx::time(0));

  // Loop has reset, so time does advance and work is done.
  EXPECT_TRUE(RunLoopFor(zx::sec(1)));
  EXPECT_EQ(Now(), zx::time(0) + zx::sec(1));

  // Quit task is posted, followed by another task. The quit task is
  // dispatched and work is reported.
  async::PostTask(dispatcher(), [this] { QuitLoop(); });
  async::PostTask(dispatcher(), [] {});
  EXPECT_TRUE(RunLoopUntilIdle());

  // Loop was quit, but it is now reset  the remaining task will be dispatched
  // on the next run.
  EXPECT_TRUE(RunLoopUntilIdle());
}


}  // namespace
}  // namespace gtest
