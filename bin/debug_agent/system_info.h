// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include <zircon/types.h>
#include <zx/process.h>

#include "garnet/lib/debug_ipc/records.h"

// Fills the root with the process tree of the current system.
zx_status_t GetProcessTree(debug_ipc::ProcessTreeRecord* root);

// Returns a process handle for the given process koid. The process will be
// not is_valid() on failure.
zx::process GetProcessFromKoid(zx_koid_t koid);
