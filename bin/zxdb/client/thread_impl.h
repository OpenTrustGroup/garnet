// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ProcessImpl;

class ThreadImpl : public Thread {
 public:
  ThreadImpl(ProcessImpl* process, const debug_ipc::ThreadRecord& record);
  ~ThreadImpl() override;

  // Thread implementation:
  Process* GetProcess() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;
  debug_ipc::ThreadRecord::State GetState() const override;
  void Pause() override;
  void Continue() override;
  void StepInstruction() override;
  const std::vector<std::unique_ptr<Frame>>& GetFrames() const override;
  bool HasAllFrames() const override;
  void SyncFrames(std::function<void()> callback) override;

  // Updates the thread metadata with new state from the agent.
  void SetMetadata(const debug_ipc::ThreadRecord& record);

  // Notification from the agent of an exception.
  void OnException(const debug_ipc::NotifyException& notify);

 private:
  void HaveFrames(const std::vector<debug_ipc::StackFrame>& frames);

  // Invlidates the cached frames.
  void ClearFrames();

  ProcessImpl* const process_;
  uint64_t koid_;
  std::string name_;
  debug_ipc::ThreadRecord::State state_;

  std::vector<std::unique_ptr<Frame>> frames_;
  bool has_all_frames_ = false;

  fxl::WeakPtrFactory<ThreadImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb
