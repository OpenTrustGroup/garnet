// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

__BEGIN_CDECLS;

typedef struct wdt_protocol_ops {
    // returns wdt controller info
    zx_status_t (*wdt_enable)(void* ctx, zx_handle_t* handle_out);
} wdt_protocol_ops_t;

typedef struct wdt_protocol {
    wdt_protocol_ops_t* ops;
    void* ctx;
} wdt_protocol_t;

__END_CDECLS;

