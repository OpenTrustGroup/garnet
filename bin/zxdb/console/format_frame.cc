// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_frame.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/value.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_value.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

void ListCompletedFrames(Thread* thread, bool long_format) {
  Console* console = Console::get();
  int active_frame_id = console->context().GetActiveFrameIdForThread(thread);

  auto helper = fxl::MakeRefCounted<FormatValue>();

  // This doesn't use table output since the format of the stack frames is
  // usually so unpredictable.
  const auto& frames = thread->GetFrames();
  if (frames.empty()) {
    helper->Append("No stack frames.\n");
  } else {
    for (int i = 0; i < static_cast<int>(frames.size()); i++) {
      if (i == active_frame_id)
        helper->Append(GetRightArrow() + " ");
      else
        helper->Append("  ");

      helper->Append(OutputBuffer::WithContents(Syntax::kSpecial,
                                                fxl::StringPrintf("%d ", i)));

      // Supply "-1" for the frame index to suppress printing (we already
      // did it above).
      if (long_format) {
        FormatFrameLong(frames[i], helper.get(), FormatValueOptions(), -1);
      } else {
        OutputBuffer out;
        FormatFrame(frames[i], &out, -1);
        helper->Append(std::move(out));
      }

      helper->Append("\n");
    }
  }

  helper->Complete(
      [helper](OutputBuffer out) { Console::get()->Output(std::move(out)); });
}

}  // namespace

void OutputFrameList(Thread* thread, bool long_format) {
  if (thread->HasAllFrames()) {
    ListCompletedFrames(thread, long_format);
  } else {
    thread->SyncFrames([ thread = thread->GetWeakPtr(), long_format ]() {
      Console* console = Console::get();
      if (thread)
        ListCompletedFrames(thread.get(), long_format);
      else
        console->Output("Thread exited, no frames.\n");
    });
  }
}

void FormatFrame(const Frame* frame, OutputBuffer* out, int id) {
  if (id >= 0)
    out->Append(fxl::StringPrintf("Frame %d ", id));
  out->Append(DescribeLocation(frame->GetLocation(), false));
}

void FormatFrameLong(const Frame* frame, FormatValue* out,
                     const FormatValueOptions& options, int id) {
  if (id >= 0)
    out->Append(OutputBuffer::WithContents(fxl::StringPrintf("Frame %d ", id)));

  // Only print the location if it has symbols, otherwise the hex
  // address will be shown twice.
  const Location& location = frame->GetLocation();
  if (location.has_symbols())
    out->Append(DescribeLocation(location, false));

  // Long format includes the IP address.
  out->Append(OutputBuffer(Syntax::kComment, fxl::StringPrintf(
      "\n      IP = 0x%" PRIx64 ", BP = 0x%" PRIx64 ", SP = 0x%" PRIx64,
      frame->GetAddress(), frame->GetBasePointer(), frame->GetStackPointer())));

  if (location.function()) {
    const Function* func = location.function().Get()->AsFunction();
    if (func) {
      // Always list function parameters in the order specified.
      for (const auto& param : func->parameters()) {
        const Variable* value = param.Get()->AsVariable();
        if (!value)
          continue;  // Symbols are corrupt.

        out->Append("\n      ");  // Indent.
        out->AppendVariableWithName(location.symbol_context(),
                                    frame->GetSymbolDataProvider(), value,
                                    options);
      }
    }
  }
}

}  // namespace zxdb
