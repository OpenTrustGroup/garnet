// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace zxdb {

// A FrameFingerprint is a way to compare stack frames across pause/resumes of
// the same thread. The Frame pointers themselves are owned by the Thread and
// will be destroyed when the thread is resumed. By saving a FrameFingerprint
// code can compare whether a future stop is the same or a subframe of the
// previous one.
//
// With stack frame pointers, an x64 prologue looks like this:
//   push rbp
//   mov rbp, rsp
//
// The stack grows to smaller addresses as stuff is pushed (in this diagram,
// down). Before the call say it looks like this:
//   0x1010 [data]      <= BP
//   0x1008 [data]
//   0x1000 [data]      <= SP
//   ...... [garbage]
//
// The CALL instruction will make it look like this:
//   0x1010 [data]      <= BP (same as before call)
//   0x1008 [data]
//   0x1000 [data]      <= FrameFingerprint.frame_address_
//   0x0ff8 [ret addr]  <= SP (new)
//
// And after the called function's prologue it will look like this:
//   0x1010 [data]
//   0x1008 [data]
//   0x1000 [data]      <= FrameFingerprint.frame_address_
//   0x0ff8 [ret addr]
//   0x0ff0 [old BP]    <= BP, SP (both new)
//   ...... [function locals will be appended starting here]
//
// Ideally we want a consistent way to refer to this stack frame that doesn't
// change across the function prologue. GDB and LLDB use a "frame_id" (GDB) /
// "FrameID" (LLDB) which is a combination of the "stack_addr" and "code_addr".
// Together these uniquely identify a stack frame.
//
// Their "code_addr" is the address of the beginning of the function it's
// currently in (the destination of the call above). This is easy enough to get
// from Location.function().
//
// Their "stack_addr" for the function being called in this example will be
// 0x1000 which is the SP from right before the call instruction. We can get
// this by looking at the previous frame's SP.
//
// Because the "frame address" is actually the stack pointer of the previous
// frame, the getter for this object is on the Thread (GetFrameFingerprint).
class FrameFingerprint {
 public:
  FrameFingerprint() = default;

  // We currently don't have a use for "function begin" so it is not included
  // here. It may be necessary in the future.
  explicit FrameFingerprint(uint64_t frame_address)
      : frame_address_(frame_address) {}

  bool is_valid() const { return frame_address_ != 0; }

  // Returns true if the input refers to the same frame as this one. This
  // will assert if either frame is !is_valid().
  bool operator==(const FrameFingerprint& other) const;

  // Computes "left Newer than right". This doesn't use operator < or > because
  // it's ambiguous whether a newer frame is "less" or "greater".
  static bool Newer(const FrameFingerprint& left, const FrameFingerprint& right);

 private:
  // The address of the stack immediately before the function call (i.e. the
  // stack pointer of the previous frame). See the class documentation above.
  uint64_t frame_address_ = 0;
};

}  // namespace zxdb
