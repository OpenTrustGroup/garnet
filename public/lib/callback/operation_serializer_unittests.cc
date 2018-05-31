// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/operation_serializer.h"

#include "gtest/gtest.h"
#include "lib/fxl/functional/closure.h"

namespace callback {
namespace {

TEST(OperationSerializer, ExecutionIsInOrder) {
  const int N = 10;

  OperationSerializer operation_serializer;
  // Add in serializer the N callbacks: each of them stores the operation to be
  // executed later.
  fxl::Closure execute_later[N];
  bool called[N] = {false};
  for (int i = 0; i < N; ++i) {
    operation_serializer.Serialize<>([] {},
                                     [&, i](std::function<void()> operation) {
                                       called[i] = true;
                                       execute_later[i] = std::move(operation);
                                     });
  }

  // In the begining only the first serializer callback must be called. The
  // rest should be blocked until the first operation is executed.
  EXPECT_TRUE(called[0]);
  for (int i = 1; i < N; ++i) {
    EXPECT_FALSE(called[i]);
  }

  // Execute the operations one by one, and make sure the following ones have
  // not yet been executed.
  for (int i = 0; i < N; ++i) {
    execute_later[i]();
    // Executing operation i unblocks the following one.
    if (i != N - 1) {
      EXPECT_TRUE(called[i + 1]);
      EXPECT_FALSE(operation_serializer.empty());
    }
    // But, until the operation (i+1) is executed, all following ones are still
    // blocked.
    for (int j = i + 2; j < N; ++j) {
      EXPECT_FALSE(called[j]);
    }
  }
  EXPECT_TRUE(operation_serializer.empty());
}

TEST(OperationSerializer, DontContinueAfterDestruction) {
  bool called_1 = false;
  auto op_1 = [&called_1] { called_1 = true; };

  bool called_2 = false;
  auto op_2 = [&called_2] { called_2 = true; };

  std::function<void()> execute_later;
  {
    OperationSerializer operation_serializer;
    operation_serializer.Serialize<>(
        std::move(op_1), [&execute_later](std::function<void()> operation) {
          // Store the operation to execute it
          // later.
          execute_later = std::move(operation);
        });
    operation_serializer.Serialize<>(
        std::move(op_2), [](std::function<void()> operation) { operation(); });

    // Since the first operation is not yet executed, the second one is also
    // blocked.
    EXPECT_FALSE(operation_serializer.empty());
    EXPECT_FALSE(called_1);
    EXPECT_FALSE(called_2);
  }
  // |operation_serializer| is now deleted. Make sure that the first operation
  // is executed, because serializer started the first operation before being
  // destroyed, but the second one isn't.
  execute_later();
  EXPECT_TRUE(called_1);
  EXPECT_FALSE(called_2);
}

}  // namespace
}  // namespace callback
