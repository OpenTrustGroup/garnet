// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_task_sync.h"

#include <condition_variable>
#include <mutex>

#include "lib/fxl/logging.h"

namespace btlib {
namespace common {

void RunTaskSync(const fxl::Closure& callback,
                 fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(callback);
  FXL_DCHECK(task_runner);

  if (task_runner->RunsTasksOnCurrentThread()) {
    callback();
    return;
  }

  std::mutex mtx;
  std::condition_variable cv;
  bool done = false;

  task_runner->PostTask([callback, &mtx, &cv, &done] {
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
