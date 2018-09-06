// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame_symbol_data_provider.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

FrameSymbolDataProvider::FrameSymbolDataProvider(Frame* frame)
    : frame_(frame) {}

FrameSymbolDataProvider::~FrameSymbolDataProvider() = default;

void FrameSymbolDataProvider::DisownFrame() { frame_ = nullptr; }

bool FrameSymbolDataProvider::GetRegister(int dwarf_register_number,
                                          uint64_t* output) {
  if (!frame_)
    return false;

  if (dwarf_register_number == kRegisterIP) {
    *output = frame_->GetAddress();
    return true;
  }
  if (dwarf_register_number == kRegisterBP) {
    *output = frame_->GetBasePointer();
    return true;
  }

  // TODO(brettw) enable synchronous access if the registers are cached.
  // See GetRegisterAsync().
  return false;
}

void FrameSymbolDataProvider::GetRegisterAsync(int dwarf_register_number,
                                               GetRegisterCallback callback) {
  // TODO(brettw) registers are not available except when this frame is the
  // top stack frame. Currently, there is no management of this and the frame
  // doesn't get notifications when it's topmost or not, and whether the thread
  // has been resumed (both things would invalidate cached registers). As
  // a result, currently we do not cache register values and always do a
  // full async request for each one.
  //
  // Additionally, some registers can be made available in non-top stack
  // frames. Libunwind should be able to tell us the saved registers for older
  // stack frames.
  if (!frame_ || !IsTopFrame()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [ dwarf_register_number, cb = std::move(callback) ]() {
          cb(Err(fxl::StringPrintf("Register %d unavailable.",
                                   dwarf_register_number)),
             0);
        });
    return;
  }

  frame_->GetThread()->GetRegisters([
    dwarf_register_number, cb = std::move(callback)
  ](const Err& err, const RegisterSet& regs) {
    uint64_t value = 0;
    if (err.has_error()) {
      cb(err, 0);
    } else if (regs.GetRegisterValueFromDWARF(dwarf_register_number, &value)) {
      cb(Err(), value);  // Success.
    } else {
      cb(Err(fxl::StringPrintf("Register %d unavailable.",
                               dwarf_register_number)),
         0);
    }
  });
}

void FrameSymbolDataProvider::GetMemoryAsync(uint64_t address, uint32_t size,
                                             GetMemoryCallback callback) {
  if (!frame_) {
    debug_ipc::MessageLoop::Current()->PostTask([cb = std::move(callback)]() {
      cb(Err("Call frame destroyed."), std::vector<uint8_t>());
    });
    return;
  }

  // Mistakes may make extremely large memory requests which can OOM the
  // system. Prevent those.
  if (size > 1024 * 1024) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [ address, size, cb = std::move(callback) ]() {
          cb(Err(fxl::StringPrintf("Memory request for %u bytes at 0x%" PRIx64
                                   " is too large.",
                                   size, address)),
             std::vector<uint8_t>());
        });
    return;
  }

  frame_->GetThread()->GetProcess()->ReadMemory(
      address, size, [ address, size, cb = std::move(callback) ](
                         const Err& err, MemoryDump dump) {
        if (err.has_error()) {
          cb(err, std::vector<uint8_t>());
          return;
        }

        FXL_DCHECK(size == 0 || dump.address() == address);
        FXL_DCHECK(dump.size() == size);
        if (dump.blocks().size() == 1 ||
            (dump.blocks().size() > 1 && !dump.blocks()[1].valid)) {
          // Common case: came back as one block OR it read until an invalid
          // memory boundary and the second block is invalid.
          //
          // In both these cases we can directly return the first data block.
          // We don't have to check the first block's valid flag since if it's
          // not valid it will be empty, which is what our API specifies.
          cb(Err(), std::move(dump.blocks()[0].data));
        } else {
          // The debug agent doesn't guarantee that a memory dump will exist in
          // only one block even if the memory is all valid. Flatten all
          // contiguous valid regions to a single buffer.
          std::vector<uint8_t> flat;
          flat.reserve(dump.size());
          for (const auto block : dump.blocks()) {
            if (!block.valid)
              break;
            flat.insert(flat.end(), block.data.begin(), block.data.end());
          }
          cb(Err(), std::move(flat));
        }
      });
}

bool FrameSymbolDataProvider::IsTopFrame() const {
  if (!frame_)
    return false;
  auto frames = frame_->GetThread()->GetFrames();
  return frames[0] == frame_;
}

}  // namespace zxdb
