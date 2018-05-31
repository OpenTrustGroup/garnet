// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

class Frame;
class Process;

class Thread : public ClientObject {
 public:
  explicit Thread(Session* session);
  ~Thread() override;

  void AddObserver(ThreadObserver* observer);
  void RemoveObserver(ThreadObserver* observer);

  // Guaranteed non-null.
  virtual Process* GetProcess() const = 0;

  virtual uint64_t GetKoid() const = 0;
  virtual const std::string& GetName() const = 0;

  // The state of the thread isn't necessarily up-to-date. There are no
  // system messages for a thread transitioning to suspended, for example.
  // To make sure this is up-to-date, call Process::SyncThreads().
  virtual debug_ipc::ThreadRecord::State GetState() const = 0;

  // Applies only to this thread (other threads will continue to run or not run
  // as they were previously).
  virtual void Pause() = 0;
  virtual void Continue() = 0;
  virtual void StepInstruction() = 0;

  // Access to the stack frames for this thread at its current stopped
  // position. If a thread is running, the stack frames are not available.
  //
  // When a thread is stopped, it will have only its first frame available
  // by default (the current IP and stack position). So stopped threads will
  // always have at least one result in the vector returned by GetFrames().
  //
  // If the full backtrace is needed, SyncFrames() can be called which will
  // compute the full backtrace and issue the callback when complete. This
  // backtrace will be cached until the thread is resumed. HasAllFrames()
  // will return true if the full backtrace is currently available (= true) or
  // if only the current position is available (= false).
  //
  // Since the running/stopped state of a thread isn't available synchronously
  // in a non-racy manner, you can always request a Sync of the frames if the
  // frames are not all available. If the thread is running when the request
  // is processed, the callback will be issued. A subsequent call to
  // GetFrames() will return an empty vector and HasAllFrames() will return
  // false.
  //
  // The vector returned by GetFrames will be an internal one that will change
  // when the thread is resumed. The pointers in the vector can be cached if
  // the code listens for ThreadObserver::OnThreadFramesInvalidated() and
  // clears the cache at that point.
  virtual std::vector<Frame*> GetFrames() const = 0;
  virtual bool HasAllFrames() const = 0;
  virtual void SyncFrames(std::function<void()> callback) = 0;

 protected:
  fxl::ObserverList<ThreadObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<ThreadObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace zxdb
