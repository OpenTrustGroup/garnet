// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/block.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sync/completion.h>
#include <zircon/types.h>

struct guest_ctx;
struct vblock_drv;
struct vpart_iso_drv;

typedef struct guest_ctx guest_ctx_t;
typedef struct vblock_drv vblock_drv_t;
typedef struct vpart_iso_drv vpart_iso_drv_t;

typedef struct {
  completion_t completion;
  zx_status_t status;
} sync_read_ctx_t;

zx_status_t vpart_isolate_init(guest_ctx_t* guest, vblock_drv_t* dev, int vmid);
void vpart_isolate_release(guest_ctx_t* guest);
void vpart_isolate_get_vbinfo(guest_ctx_t* guest, uint64_t* vblock_count);
zx_status_t vpart_isolate_process_bop(guest_ctx_t* guest,
                                      block_op_t* bop,
                                      bool* rgpt_flag);

zx_status_t vpart_isolate_create(block_protocol_t* block_proto,
                                 block_info_t* block_info,
                                 size_t block_op_size,
                                 int vmid,
                                 vpart_iso_drv_t** out_vpi,
                                 bool* out_has_partition_isolation);
void vpart_isolate_destroy(vpart_iso_drv_t* vpi_drv);
uint64_t vpart_isolate_get_block_count(vpart_iso_drv_t* vpi_drv);
zx_status_t vpart_isolate_process(vpart_iso_drv_t* vpi_drv,
                                  block_op_t* bop,
                                  bool* rgpt_flag);
