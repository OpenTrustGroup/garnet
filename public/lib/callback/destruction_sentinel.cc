// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/destruction_sentinel.h"

namespace callback {

DestructionSentinel::DestructionSentinel() {}

DestructionSentinel::~DestructionSentinel() {
  if (is_destructed_ptr_)
    *is_destructed_ptr_ = true;
}

}  // namespace callback
