// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/a11y/a11y_touch_dispatcher/app.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  a11y_touch_dispatcher::App app;
  loop.Run();
  return 0;
}
