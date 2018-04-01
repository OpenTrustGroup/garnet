// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do not include directly, use "arch.h".

#pragma once

#include <stdint.h>
#include <zircon/syscalls/debug.h>

namespace arch {

// The type that is large enough to hold the debug breakpoint CPU instruction.
using BreakInstructionType = uint32_t;

}  // namespace arch
