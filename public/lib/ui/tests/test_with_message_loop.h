// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_TEST_WITH_MESSAGE_LOOP_H_
#define LIB_UI_TESTS_TEST_WITH_MESSAGE_LOOP_H_

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

// Run message loop *while* condition is true (timeout after 400*10ms = 4000ms)
#define RUN_MESSAGE_LOOP_WHILE(condition)                             \
  {                                                                   \
    for (int i = 0; condition && i < 400; i++) {                      \
      ::scenic::test::RunLoopWithTimeout(                             \
          fxl::TimeDelta::FromMilliseconds(10));                      \
    }                                                                 \
    ASSERT_FALSE(condition) << "Message loop timeout must not occur"; \
  }

// Run message loop *until* condition is true (timeout after 400*10ms = 4000ms)
#define RUN_MESSAGE_LOOP_UNTIL(condition)                            \
  {                                                                  \
    for (int i = 0; !(condition) && i < 400; i++) {                  \
      ::scenic::test::RunLoopWithTimeout(                            \
          fxl::TimeDelta::FromMilliseconds(10));                     \
    }                                                                \
    ASSERT_TRUE(condition) << "Message loop timeout must not occur"; \
  }

namespace scenic {
namespace test {

// Run the loop for at most |timeout|. Returns |true| if the timeout has
// been reached.
bool RunLoopWithTimeout(
    fxl::TimeDelta timeout = fxl::TimeDelta::FromSeconds(1));

}  // namespace test
}  // namespace scenic

#endif  // LIB_UI_TESTS_TEST_WITH_MESSAGE_LOOP_H_
