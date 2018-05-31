// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class ThreadImpl;

// A frame is lazily symbolized.
class FrameImpl : public Frame {
 public:
  FrameImpl(ThreadImpl* thread,
            const debug_ipc::StackFrame& stack_frame,
            Location location);
  ~FrameImpl() override;

  // Frame implementation.
  Thread* GetThread() const override;
  const Location& GetLocation() const override;

  void EnsureSymbolized();

 private:
  ThreadImpl* thread_;

  debug_ipc::StackFrame stack_frame_;
  Location location_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameImpl);
};

}  // namespace zxdb
