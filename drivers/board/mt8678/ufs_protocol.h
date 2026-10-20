// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

__BEGIN_CDECLS;

typedef struct ufs_protocol_ops {
	//returns ufs controller info
	zx_status_t (*get_info)(void* ctx, zx_handle_t* handle_out);
} ufs_protocol_ops_t;
	    
typedef struct ufs_protocol {
	ufs_protocol_ops_t* ops;
	void* ctx;
} ufs_protocol_t;

__END_CDECLS;
