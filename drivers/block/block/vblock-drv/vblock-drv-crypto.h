// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/block.h>
#include <virtio/vblock.h>

struct guest_ctx;

void bop_set_crypto(struct guest_ctx *guest, block_op_t *bop,
                    req_hdr_t const *hdr, void *crypto_ctx_alloc);

void virtio_blk_evict_all_keys(struct guest_ctx *guest);
