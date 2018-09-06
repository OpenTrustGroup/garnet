// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/x86/e820.h"

#include <zircon/boot/e820.h>

#include "garnet/lib/machina/address.h"

// clang-format off

static constexpr uint64_t kAddr32kb     = 0x0000000000008000;
static constexpr uint64_t kAddr512kb    = 0x0000000000080000;
static constexpr uint64_t kAddr1mb      = 0x0000000000100000;

// clang-format on

namespace machina {

size_t e820_entries(size_t size) { return size > kAddr1mb ? 4 : 3; }

void create_e820(void* addr, size_t size) {
  auto entry = static_cast<e820entry_t*>(addr);
  // 0 to 32kb is reserved.
  entry[0].addr = 0;
  entry[0].size = kAddr32kb;
  entry[0].type = E820_RESERVED;
  // 32kb to to 512kb is available (for Linux's real mode trampoline).
  entry[1].addr = kAddr32kb;
  entry[1].size = kAddr512kb - kAddr32kb;
  entry[1].type = E820_RAM;
  // 512kb to 1mb is reserved.
  entry[2].addr = kAddr512kb;
  entry[2].size = kAddr1mb - kAddr512kb;
  entry[2].type = E820_RESERVED;
  if (size > kAddr1mb) {
    // 1mb to min(size, 1mb) is available.
    entry[3].addr = kAddr1mb;
    entry[3].size = size - kAddr1mb;
    entry[3].type = E820_RAM;
  }
}

}  // namespace machina
