// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2026 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/logging_rate_limiter.h"

namespace fxl {

// The constructor passes these to std::chrono::milliseconds, whose converting
// constructor takes its argument by reference, which odr-uses them.  Before
// C++17 a static constexpr member declared in the class still needs a
// definition out of line, or the reference is left undefined.  An optimizing
// build folds the read away and never asks for it, which is why this has gone
// unnoticed: the DDK self-test builds unoptimized and asked.
constexpr uint64_t LogRateLimiter::DEFAULT_INTERVAL_MS;
constexpr uint32_t LogRateLimiter::DEFAULT_BURST;

}  // namespace fxl
