// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"

#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait_with_timeout.h>
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace fsl {
namespace {

thread_local MessageLoop* g_current;

}  // namespace

class MessageLoop::TaskRecord {
 public:
  TaskRecord(zx_time_t deadline, fxl::Closure closure);
  ~TaskRecord();

  async::Task& task() { return task_; }

 private:
  async::Task task_;
};

class MessageLoop::HandlerRecord {
 public:
  HandlerRecord(zx_handle_t object,
                zx_signals_t trigger,
                zx_time_t deadline,
                MessageLoop* loop,
                MessageLoopHandler* handler,
                HandlerKey key);
  ~HandlerRecord();

  async::WaitWithTimeout& wait() { return wait_; }

 private:
  async_wait_result_t Handle(async_t* async,
                             zx_status_t status,
                             const zx_packet_signal_t* signal);

  async::WaitWithTimeout wait_;
  MessageLoop* loop_;
  MessageLoopHandler* handler_;
  HandlerKey key_;
};

MessageLoop::MessageLoop()
    : MessageLoop(fxl::MakeRefCounted<internal::IncomingTaskQueue>()) {}

MessageLoop::MessageLoop(
    fxl::RefPtr<internal::IncomingTaskQueue> incoming_tasks)
    : loop_config_{.make_default_for_current_thread = true,
                   .epilogue = &MessageLoop::Epilogue,
                   .data = this},
      loop_(&loop_config_),
      task_runner_(std::move(incoming_tasks)) {
  FXL_DCHECK(!g_current) << "At most one message loop per thread.";
  g_current = this;

  MessageLoop::incoming_tasks()->InitDelegate(this);
}

MessageLoop::~MessageLoop() {
  FXL_DCHECK(g_current == this)
      << "Message loops must be destroyed on their own threads.";

  loop_.Shutdown();
  FXL_DCHECK(handlers_.empty());

  incoming_tasks()->ClearDelegate();

  g_current = nullptr;
}

MessageLoop* MessageLoop::GetCurrent() {
  return g_current;
}

void MessageLoop::PostTask(fxl::Closure task, fxl::TimePoint target_time) {
  // TODO(jeffbrown): Consider allocating tasks from a pool.
  auto record = new TaskRecord(target_time.ToEpochDelta().ToNanoseconds(),
                               std::move(task));

  zx_status_t status = record->task().Post(loop_.async());
  if (status == ZX_ERR_BAD_STATE) {
    // Suppress request when shutting down.
    delete record;
    return;
  }

  // The record will be destroyed when the task runs.
  FXL_CHECK(status == ZX_OK) << "Failed to post task: status=" << status;
}

MessageLoop::HandlerKey MessageLoop::AddHandler(MessageLoopHandler* handler,
                                                zx_handle_t handle,
                                                zx_signals_t trigger,
                                                fxl::TimeDelta timeout) {
  FXL_DCHECK(g_current == this);
  FXL_DCHECK(handler);
  FXL_DCHECK(handle != ZX_HANDLE_INVALID);

  // TODO(jeffbrown): Consider allocating handlers from a pool.
  HandlerKey key = next_handler_key_++;
  auto record =
      new HandlerRecord(handle, trigger,
                        timeout == fxl::TimeDelta::Max()
                            ? ZX_TIME_INFINITE
                            : zx_deadline_after(timeout.ToNanoseconds()),
                        this, handler, key);
  zx_status_t status = record->wait().Begin(loop_.async());
  if (status == ZX_ERR_BAD_STATE) {
    // Suppress request when shutting down.
    delete record;
    return key;
  }

  // The record will be destroyed when the handler runs or is removed.
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
  handlers_.emplace(key, record);
  return key;
}

void MessageLoop::RemoveHandler(HandlerKey key) {
  FXL_DCHECK(g_current == this);

  auto it = handlers_.find(key);
  if (it == handlers_.end())
    return;

  HandlerRecord* record = it->second;
  handlers_.erase(it);

  if (current_handler_ == record) {
    current_handler_removed_ = true;  // defer cleanup
  } else {
    zx_status_t status = record->wait().Cancel(loop_.async());
    FXL_CHECK(status == ZX_OK) << "Failed to cancel handler: status=" << status;
    delete record;
  }
}

bool MessageLoop::HasHandler(HandlerKey key) const {
  FXL_DCHECK(g_current == this);

  return handlers_.find(key) != handlers_.end();
}

void MessageLoop::Run(bool until_idle) {
  FXL_DCHECK(g_current == this);

  FXL_CHECK(!is_running_) << "Cannot run a nested message loop.";
  is_running_ = true;

  zx_status_t status = until_idle ? loop_.RunUntilIdle() : loop_.Run();
  FXL_CHECK(status == ZX_OK || status == ZX_ERR_CANCELED)
      << "Loop stopped abnormally: status=" << status;

  status = loop_.ResetQuit();
  FXL_DCHECK(status == ZX_OK)
      << "Failed to reset quit state: status=" << status;

  FXL_DCHECK(is_running_);
  is_running_ = false;
}

void MessageLoop::Run() {
  Run(false);
}

void MessageLoop::RunUntilIdle() {
  Run(true);
}

void MessageLoop::QuitNow() {
  FXL_DCHECK(g_current == this);

  if (is_running_)
    loop_.Quit();
}

void MessageLoop::PostQuitTask() {
  task_runner()->PostTask([this]() { QuitNow(); });
}

bool MessageLoop::RunsTasksOnCurrentThread() {
  return g_current == this;
}

void MessageLoop::SetAfterTaskCallback(fxl::Closure callback) {
  FXL_DCHECK(g_current == this);

  after_task_callback_ = std::move(callback);
}

void MessageLoop::ClearAfterTaskCallback() {
  FXL_DCHECK(g_current == this);

  after_task_callback_ = fxl::Closure();
}

void MessageLoop::Epilogue(async_t* async, void* data) {
  auto loop = static_cast<MessageLoop*>(data);
  if (loop->after_task_callback_)
    loop->after_task_callback_();
}

MessageLoop::TaskRecord::TaskRecord(zx_time_t deadline, fxl::Closure closure)
    : task_(deadline, ASYNC_FLAG_HANDLE_SHUTDOWN) {
  task_.set_handler(
      [this, closure = std::move(closure)](async_t*, zx_status_t status) {
        if (status == ZX_OK)
          closure();
        delete this;
        return ASYNC_TASK_FINISHED;
      });
}

MessageLoop::TaskRecord::~TaskRecord() = default;

MessageLoop::HandlerRecord::HandlerRecord(zx_handle_t object,
                                          zx_signals_t trigger,
                                          zx_time_t deadline,
                                          MessageLoop* loop,
                                          MessageLoopHandler* handler,
                                          HandlerKey key)
    : wait_(object, trigger, deadline, ASYNC_FLAG_HANDLE_SHUTDOWN),
      loop_(loop),
      handler_(handler),
      key_(key) {
  wait_.set_handler(fbl::BindMember(this, &MessageLoop::HandlerRecord::Handle));
}

MessageLoop::HandlerRecord::~HandlerRecord() = default;

async_wait_result_t MessageLoop::HandlerRecord::Handle(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  FXL_DCHECK(!loop_->current_handler_);
  loop_->current_handler_ = this;

  if (status == ZX_OK) {
    handler_->OnHandleReady(wait_.object(), signal->observed, signal->count);
  } else {
    handler_->OnHandleError(wait_.object(), status);

    if (!loop_->current_handler_removed_) {
      auto it = loop_->handlers_.find(key_);
      FXL_DCHECK(it != loop_->handlers_.end());
      loop_->handlers_.erase(it);
      loop_->current_handler_removed_ = true;
    }
  }

  FXL_DCHECK(loop_->current_handler_ == this);
  loop_->current_handler_ = nullptr;
  if (!loop_->current_handler_removed_)
    return ASYNC_WAIT_AGAIN;

  loop_->current_handler_removed_ = false;
  delete this;
  return ASYNC_WAIT_FINISHED;
}

}  // namespace fsl
