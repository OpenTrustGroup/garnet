// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/mock_frame.h"

#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

MockFrame::MockFrame(Session* session, Thread* thread,
                     const debug_ipc::StackFrame& stack_frame,
                     const Location& location)
    : Frame(session),
      thread_(thread),
      stack_frame_(stack_frame),
      location_(location) {}
MockFrame::~MockFrame() = default;

Thread* MockFrame::GetThread() const { return thread_; }
const Location& MockFrame::GetLocation() const { return location_; }
uint64_t MockFrame::GetAddress() const { return stack_frame_.ip; }
uint64_t MockFrame::GetBasePointer() const { return stack_frame_.bp; }
uint64_t MockFrame::GetStackPointer() const { return stack_frame_.sp; }

fxl::RefPtr<SymbolDataProvider> MockFrame::GetSymbolDataProvider() const {
  if (!symbol_data_provider_)
    symbol_data_provider_ = fxl::MakeRefCounted<MockSymbolDataProvider>();
  return symbol_data_provider_;
}

fxl::RefPtr<ExprEvalContext> MockFrame::GetExprEvalContext() const {
  if (!symbol_eval_context_) {
    symbol_eval_context_ = fxl::MakeRefCounted<SymbolEvalContext>(
        GetSymbolDataProvider(), location_);
  }
  return symbol_eval_context_;
}

}  // namespace zxdb
