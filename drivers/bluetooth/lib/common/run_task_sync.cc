// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_task_sync.h"

#include <condition_variable>
#include <mutex>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fxl/logging.h"

namespace btlib {
namespace common {

void RunTaskSync(const fxl::Closure& callback, async_t* dispatcher) {
  FXL_DCHECK(callback);

  // TODO(armansito) This check is risky. async_get_default() could return
  // a dispatcher that goes to another thread. We don't have any current
  // instances of a multi threaded dispatcher but we could.
  if (dispatcher == async_get_default()) {
    callback();
    return;
  }

  std::mutex mtx;
  std::condition_variable cv;
  bool done = false;

  async::PostTask(dispatcher, [callback, &mtx, &cv, &done] {
    callback();

    {
      std::lock_guard<std::mutex> lock(mtx);
      done = true;
    }

    cv.notify_one();
  });

  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&done] { return done; });
}

}  // namespace common
}  // namespace btlib
