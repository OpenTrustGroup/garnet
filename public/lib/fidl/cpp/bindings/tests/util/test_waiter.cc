// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/loop.h>
#include <lib/async/default.h>

#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"

namespace f1dl {
namespace test {

void InitAsyncWaiter() {
  async_loop_config_t config = {};
  config.make_default_for_current_thread = true;
  async_t* async = nullptr;
  async_loop_create(&config, &async);
}

void WaitForAsyncWaiter() {
  async_loop_run(async_get_default(), 0, false);
}

void ClearAsyncWaiter() {
  async_loop_destroy(async_get_default());
  InitAsyncWaiter();
}

}  // namespace test
}  // namespace f1dl
