// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

// Retrieves the async_dispatcher_t* for this driver.
async_dispatcher_t* wlanphy_async_t();
