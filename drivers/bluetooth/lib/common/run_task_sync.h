// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

#include "lib/fxl/functional/closure.h"

namespace btlib {
namespace common {

// Posts |callback| on |dispatcher| and waits for it to finish running.
// |callback| will always finish running before this function returns.
// |dispatcher| cannot be bound to the thread on which this function gets
// called.
//
// NOTE: This should generally be avoided. This is primarily intended for
// synchronous setup/shutdown sequences and unit tests.
void RunTaskSync(const fxl::Closure& callback, async_t* dispatcher);

}  // namespace common
}  // namespace btlib
