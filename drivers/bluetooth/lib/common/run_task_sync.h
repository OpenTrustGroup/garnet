// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_RUN_TASK_SYNC_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_RUN_TASK_SYNC_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

namespace btlib {
namespace common {

// Posts |callback| on |dispatcher| and waits for it to finish running.
// |callback| will always finish running before this function returns.
// |dispatcher| cannot be bound to the thread on which this function gets
// called.
//
// NOTE: This should generally be avoided. This is primarily intended for
// synchronous setup/shutdown sequences and unit tests.
void RunTaskSync(fit::closure callback, async_dispatcher_t* dispatcher);

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_RUN_TASK_SYNC_H_
