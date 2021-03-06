// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VOLATILE_WRITE_BLOCK_DISPATCHER_H_
#define GARNET_LIB_MACHINA_VOLATILE_WRITE_BLOCK_DISPATCHER_H_

#include <inttypes.h>
#include <sys/types.h>
#include <mutex>

#include <bitmap/rle-bitmap.h>
#include <lib/zx/vmo.h>

#include "garnet/lib/machina/block_dispatcher.h"

namespace machina {

// A dispatcher that retains writes in RAM and delegates other requests to
// another dispatcher.
class VolatileWriteBlockDispatcher : public BlockDispatcher {
 public:
  static zx_status_t Create(std::unique_ptr<BlockDispatcher> dispatcher,
                            std::unique_ptr<BlockDispatcher>* out);

  ~VolatileWriteBlockDispatcher();

  zx_status_t Flush() override;
  zx_status_t Submit() override;
  zx_status_t Read(off_t disk_offset, void* buf, size_t size) override;
  zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override;

 private:
  VolatileWriteBlockDispatcher(std::unique_ptr<BlockDispatcher> dispatcher,
                               zx::vmo vmo, uintptr_t vmar_address);

  bool ValidateBlockParams(off_t disk_offset, size_t size);

  static constexpr size_t kBlockSize = 512;
  std::mutex mutex_;
  std::unique_ptr<BlockDispatcher> dispatcher_;

  bitmap::RleBitmap bitmap_ __TA_GUARDED(mutex_);
  zx::vmo vmo_;
  uintptr_t vmar_addr_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VOLATILE_WRITE_BLOCK_DISPATCHER_H_
