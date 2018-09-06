// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "garnet/lib/debug_ipc/protocol.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class Err;
class Thread;

// Abstract base class that provides the policy decisions for various types of
// thread stepping.
class ThreadController {
 public:
  enum StopOp { kContinue, kStop };

  // How the thread should run when it is executing this controller.
  struct ContinueOp {
    // Factory helper functions.
    static ContinueOp Continue() {
      return ContinueOp();  // Defaults are good for this case.
    }
    static ContinueOp StepInstruction() {
      ContinueOp result;
      result.how = debug_ipc::ResumeRequest::How::kStepInstruction;
      return result;
    }
    static ContinueOp StepInRange(uint64_t r_begin, uint64_t r_end) {
      ContinueOp result;
      result.how = debug_ipc::ResumeRequest::How::kStepInRange;
      result.range_begin = r_begin;
      result.range_end = r_end;
      return result;
    }

    debug_ipc::ResumeRequest::How how =
        debug_ipc::ResumeRequest::How::kContinue;

    // When how == kStepInRange, these variables define the address range to
    // step in. As long as the instruction pointer is inside [range_begin,
    // range_end), execution will continue.
    uint64_t range_begin = 0;
    uint64_t range_end = 0;
  };

  ThreadController();

  virtual ~ThreadController();

  // Registers the thread with the controller. The controller will be owned
  // by the thread (possibly indirectly) so the pointer will remain valid for
  // the rest of the lifetime of the controller.
  //
  // The implementation should call set_thread() with the thread.
  //
  // When the implementation is ready, it will issue the given callback to
  // run the thread. The callback can be issued reentrantly from inside this
  // function if the controller is ready synchronously.
  //
  // If the callback does not specify an error, the thread will be resumed
  // when it is called. If the callback has an error, it will be reported and
  // the thread will remain stopped.
  virtual void InitWithThread(Thread* thread,
                              std::function<void(const Err&)> cb) = 0;

  // Returns how to continue the thread when running this controller.
  virtual ContinueOp GetContinueOp() = 0;

  // Notification that the thread has stopped. The return value indicates what
  // the thread should do in response.
  //
  // If the ThreadController returns |kStop|, its assumed the controller has
  // completed its job and it will be deleted.
  virtual StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) = 0;

 protected:
  Thread* thread() { return thread_; }
  void set_thread(Thread* thread) { thread_ = thread; }

  // Tells the owner of this class that this ThreadController has completed
  // its work. Normally returning kStop from OnThreadStop() will do this, but
  // if the controller has another way to get events (like breakpoints), it
  // may notice out-of-band that its work is done.
  //
  // This function will likely cause |this| to be deleted.
  void NotifyControllerDone();

 private:
  Thread* thread_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadController);
};

}  // namespace zxdb
