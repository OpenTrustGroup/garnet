// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_TASKS_MESSAGE_LOOP_H_
#define LIB_FSL_TASKS_MESSAGE_LOOP_H_

#include <map>

#include <lib/async-loop/cpp/loop.h>

#include "lib/fsl/tasks/incoming_task_queue.h"
#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace fsl {

class FXL_EXPORT MessageLoop : private internal::TaskQueueDelegate {
 public:
  using HandlerKey = uint64_t;

  // Constructs a message loop with an empty task queue. The message loop is
  // bound to the current thread.
  MessageLoop();

  // Constructs a message loop that will begin by draining the tasks already
  // present in the |incoming_tasks| queue. The message loop is bound to the
  // current thread.
  explicit MessageLoop(fxl::RefPtr<internal::IncomingTaskQueue> incoming_tasks);

  ~MessageLoop() override;

  // Returns the message loop associated with the current thread, if any.
  static MessageLoop* GetCurrent();

  // Gets the underlying libasync dispatcher.
  // See <async/dispatcher.h> for details on how to use this.
  async_t* async() const { return loop_.async(); }

  // Return an interface for posting tasks to this message loop.
  const fxl::RefPtr<fxl::TaskRunner>& task_runner() const {
    return task_runner_;
  }

  // The message loop will call |callback| after each task that execute and
  // after each time it signals a handler. If the message loop already has an
  // after task callback set, this function will replace it with this one.
  void SetAfterTaskCallback(fxl::Closure callback);

  // The message loop will no longer call the registered after task callback, if
  // any.
  void ClearAfterTaskCallback();

  // Causes the message loop to run tasks until |QuitNow| is called. If no tasks
  // are available, the message loop with block and wait for tasks to be posted
  // via the |task_runner|.
  void Run();

  // Runs until there are no tasks to run and no runnable handlers. This is
  // useful for unit testing, because the behavior doesn't depend on time.
  void RunUntilIdle();

  // Prevents further tasks from running and returns from |Run|. Must be called
  // while |Run| is on the stack.
  void QuitNow();

  // Posts a task to the queue that calls |QuitNow|. Useful for gracefully
  // ending the message loop. Can be called whether or not |Run| is on the
  // stack.
  void PostQuitTask();

 private:
  // |internal::TaskQueueDelegate| implementation:
  void PostTask(fxl::Closure task, fxl::TimePoint target_time) override;
  bool RunsTasksOnCurrentThread() override;

  void Run(bool until_idle);

  static void Epilogue(async_loop_t* loop, void* data);

  internal::IncomingTaskQueue* incoming_tasks() {
    return static_cast<internal::IncomingTaskQueue*>(task_runner_.get());
  }

  async_loop_config_t loop_config_;
  async::Loop loop_;

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fxl::Closure after_task_callback_;
  bool is_running_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

}  // namespace fsl

#endif  // LIB_FSL_TASKS_MESSAGE_LOOP_H_
