// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task_domain.h"

#include <fbl/ref_counted.h>

#include "gtest/gtest.h"

namespace btlib {
namespace common {
namespace {

class TestObject : public fbl::RefCounted<TestObject>,
                   public TaskDomain<TestObject> {
 public:
  // TestObject gets handed a an async dispatcher and does not own the thread.
  explicit TestObject(async_t* dispatcher)
      : TaskDomain<TestObject>(this, dispatcher) {}

  void ScheduleTask() {
    PostMessage([this] {
      AssertOnDispatcherThread();
      task_done = true;
    });
  }

  void ShutDown() { TaskDomain<TestObject>::ScheduleCleanUp(); }

  void CleanUp() {
    AssertOnDispatcherThread();
    cleaned_up = true;
  }

  bool task_done = false;
  bool cleaned_up = false;
};

TEST(TaskDomainTest, PostMessageAndCleanUp) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  auto obj = fbl::AdoptRef(new TestObject(loop.async()));

  // Schedule a task. This is expected to run on the |thrd_runner|.
  obj->ScheduleTask();

  // Wait for the scheduled task to run.
  loop.RunUntilIdle();

  ASSERT_TRUE(obj->task_done);
  obj->task_done = false;

  // We schedule 2 tasks. The second task should not run since it is scheduled
  // after ShutDown().

  // #1: clean up task. This won't quit the loop as the TaskDomain does not own
  // the thread.
  obj->ShutDown();

  // #2: This should not run due to #1.
  obj->ScheduleTask();

  loop.RunUntilIdle();

  EXPECT_TRUE(obj->cleaned_up);
  EXPECT_FALSE(obj->task_done);
}

}  // namespace
}  // namespace common
}  // namespace btlib
