// Copyright 2026 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_BLOCK_LOCATOR_BLOCK_LOCATOR_H_
#define GARNET_LIB_BLOCK_LOCATOR_BLOCK_LOCATOR_H_

#include <stdint.h>

#include <fbl/unique_fd.h>
#include <zircon/types.h>

namespace block_locator {

enum class OpenMode {
  kReadOnly,
  kReadWrite,
};

// Opens a GPT partition by its GPT partition name. The returned fd is already
// verified as a GPT child rather than an arbitrary block-class entry.
zx_status_t OpenGptPartitionByName(const char* name, OpenMode mode,
                                   zx_duration_t timeout,
                                   fbl::unique_fd* out);

// Opens a whole UFS logical unit by block_info.block_id. GPT child devices are
// excluded even though they inherit the parent block information.
zx_status_t OpenUfsLun(uint16_t lun, OpenMode mode, zx_duration_t timeout,
                       fbl::unique_fd* out);

}  // namespace block_locator

#endif  // GARNET_LIB_BLOCK_LOCATOR_BLOCK_LOCATOR_H_
