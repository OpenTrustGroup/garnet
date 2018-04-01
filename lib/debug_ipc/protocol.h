// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/records.h"

namespace debug_ipc {

constexpr uint32_t kProtocolVersion = 1;

#pragma pack(push, 8)

// A message consists of a MsgHeader followed by a serialized version of
// whatever struct is
// associated with that message type. Use the MessageWriter class to build this
// up, which will
// reserve room for the header and allows the structs to be appended, possibly
// dynamically.
struct MsgHeader {
  enum class Type : uint32_t {
    kNone = 0,
    kHello,
    kLaunch,
    kAttach,
    kDetach,
    kContinue,
    kProcessTree,
    kThreads,
    kReadMemory,
    kAddOrChangeBreakpoint,
    kRemoveBreakpoint,

    // The "notify" messages are sent unrequested from the agent to the client.
    kNotifyProcessExiting,
    kNotifyThreadStarting,
    kNotifyThreadExiting,
    kNotifyException,

    kNumMessages
  };

  MsgHeader() = default;
  explicit MsgHeader(Type t) : type(t) {}

  uint32_t size = 0;  // Size includes this header.
  Type type = Type::kNone;

  // The transaction ID is assigned by the sender of a request, and is echoed
  // in the reply so the transaction can be easily correlated.
  //
  // Notification messages (sent unsolicited from the agent to the client) have
  // a 0 transaction ID.
  uint32_t transaction_id = 0;

  static constexpr uint32_t kSerializedHeaderSize = sizeof(uint32_t) * 3;
};

struct HelloRequest {};
struct HelloReply {
  uint32_t version = 0;
};

struct LaunchRequest {
  // argv[0] is the app to launch.
  std::vector<std::string> argv;
};
struct LaunchReply {
  uint32_t status = 0;  // zx_status_t value from launch, ZX_OK on success.
  uint64_t process_koid = 0;
  std::string process_name;
};

// The debug agent will follow a successful AttachReply with notifications for
// all threads currently existing in the attached process.
struct AttachRequest {
  uint64_t koid;
};
struct AttachReply {
  uint32_t status = 0;  // zx_status_t value from attaching. ZX_OK on success.
  std::string process_name;
};

struct DetachRequest {
  uint64_t process_koid = 0;
};
struct DetachReply {
  uint32_t status = 0;
};

struct ContinueRequest {
  // If 0, all debugged processes will be continued.
  uint64_t process_koid = 0;

  // If 0, all threads in the given process will be continued.
  uint64_t thread_koid = 0;
};
struct ContinueReply {
};

struct ProcessTreeRequest {};
struct ProcessTreeReply {
  ProcessTreeRecord root;
};

struct ThreadsRequest {
  uint64_t process_koid = 0;
};
struct ThreadsReply {
  // If there is no such process, the threads array will be empty.
  std::vector<ThreadRecord> threads;
};

struct ReadMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  uint32_t size = 0;
};
struct ReadMemoryReply {
  std::vector<MemoryBlock> blocks;
};

struct AddOrChangeBreakpointRequest {
  uint64_t process_koid = 0;
  BreakpointSettings breakpoint;
};
struct AddOrChangeBreakpointReply {
  // If the satatus is not ZX_OK (0), the breakpoint add/change did not
  // succeded. In the case of changed breakpoints failing to modify, the
  // breakpoint with the given ID will be removed so the client and agent can
  // be in a consistent state (error always means it doesn't exist).
  uint32_t status = 0;  // zx_status_t
  std::string error_message;
};

struct RemoveBreakpointRequest {
  uint64_t process_koid = 0;
  uint32_t breakpoint_id = 0;
};
struct RemoveBreakpointReply {
};

// Data for process destroyed messages (process created messages are in
// response to launch commands so is just the reply to that message).
struct NotifyProcess {
  uint64_t process_koid = 0;
  int64_t return_code = 0;
};

// Data for thread created and destroyed messages.
struct NotifyThread {
  uint64_t process_koid = 0;
  ThreadRecord record;
};

// Data passed for exceptions.
struct NotifyException {
  enum class Type : uint32_t {
    kGeneral = 0,
    kHardware,
    kSoftware,

    kLast  // Not an actual exception type, for range checking.
  };

  uint64_t process_koid = 0;
  ThreadRecord thread;

  Type type = Type::kGeneral;

  uint64_t ip = 0;  // Instruction pointer.
  uint64_t sp = 0;  // Stack pointer.
};

#pragma pack(pop)

}  // namespace debug_ipc
