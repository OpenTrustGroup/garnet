// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <zircon/types.h>
#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/bin/debug_agent/remote_api.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_agent {

// Main state and control for the debug agent.
class DebugAgent : public RemoteAPI {
 public:
  // A MessageLoopZircon should already be set up on the current thread.
  //
  // The stream must outlive this class. It will be used to send data to the
  // client. It will not be read (that's the job of the provider of the
  // RemoteAPI).
  explicit DebugAgent(debug_ipc::StreamBuffer* stream);
  ~DebugAgent();

  debug_ipc::StreamBuffer* stream() { return stream_; }

  void RemoveDebuggedProcess(zx_koid_t process_koid);

 private:
  // RemoteAPI implementation.
  void OnHello(const debug_ipc::HelloRequest& request,
               debug_ipc::HelloReply* reply) override;
  void OnLaunch(const debug_ipc::LaunchRequest& request,
                debug_ipc::LaunchReply* reply) override;
  void OnKill(const debug_ipc::KillRequest& request,
              debug_ipc::KillReply* reply) override;
  void OnAttach(std::vector<char> serialized) override;
  void OnDetach(const debug_ipc::DetachRequest& request,
                debug_ipc::DetachReply* reply) override;
  void OnPause(const debug_ipc::PauseRequest& request,
               debug_ipc::PauseReply* reply) override;
  void OnResume(const debug_ipc::ResumeRequest& request,
                debug_ipc::ResumeReply* reply) override;
  void OnModules(const debug_ipc::ModulesRequest& request,
                 debug_ipc::ModulesReply* reply) override;
  void OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                     debug_ipc::ProcessTreeReply* reply) override;
  void OnThreads(const debug_ipc::ThreadsRequest& request,
                 debug_ipc::ThreadsReply* reply) override;
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                    debug_ipc::ReadMemoryReply* reply) override;
  void OnAddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      debug_ipc::AddOrChangeBreakpointReply* reply) override;
  void OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                          debug_ipc::RemoveBreakpointReply* reply) override;
  void OnBacktrace(const debug_ipc::BacktraceRequest& request,
                   debug_ipc::BacktraceReply* reply) override;

  // Returns the debugged process/thread for the given koid(s) or null if not
  // found.
  DebuggedProcess* GetDebuggedProcess(zx_koid_t koid);
  DebuggedThread* GetDebuggedThread(zx_koid_t process_koid,
                                    zx_koid_t thread_koid);

  // Returns a pointer to the newly created object.
  DebuggedProcess* AddDebuggedProcess(zx_koid_t process_koid,
                                      zx::process zx_proc);

  debug_ipc::StreamBuffer* stream_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedProcess>> procs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebugAgent);
};

}  // namespace debug_agent
