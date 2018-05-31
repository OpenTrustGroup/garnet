// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPES_H
#define TYPES_H

#include "magma_common_defs.h"
#include <functional>
#include <stdint.h>

using gen_pte_t = uint64_t;

using gpu_addr_t = uint64_t;

constexpr gpu_addr_t kInvalidGpuAddr = ~0;

enum CachingType {
    CACHING_NONE,
    CACHING_LLC,
    CACHING_WRITE_THROUGH,
};

enum AddressSpaceType {
    ADDRESS_SPACE_GGTT,  // Global GTT address space
    ADDRESS_SPACE_PPGTT, // Per Process GTT address space
};

enum EngineCommandStreamerId {
    RENDER_COMMAND_STREAMER,
};

#endif // TYPES_H
