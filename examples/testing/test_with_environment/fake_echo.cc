// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/testing/test_with_environment/fake_echo.h"

#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <answer>\n", argv[0]);
    return 1;
  }
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  echo2::testing::FakeEcho echo;
  echo.SetAnswer(argv[1]);
  auto context = component::StartupContext::CreateFromStartupInfo();
  context->outgoing().AddPublicService(echo.GetHandler());
  loop.Run();
  return 0;
}
