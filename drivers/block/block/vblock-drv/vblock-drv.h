// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sync/completion.h>
#include <stdatomic.h>
#include <ddk/protocol/block.h>
#include <zircon/device/vblock.h>

#include "garnet/lib/vm_id/vm_id.h"

#define VBLOCK_MAX_GUESTS GRT_BLOCK_MAX_BACKEND_VMIDS
#define BLK_CRYPTO_KEY_SLOT_NUM 32

#define VBLOCK_FIFO_COUNT 2
#define VBLOCK_FIFO_END_SELF 0
#define VBLOCK_FIFO_END_PEER 1

#define VBLOCK_BACKEND_NAME_LEN 32

#define VBLOCK_EVENT_INTR 0
#define VBLOCK_EVENT_NOTIFY 1

struct vpart_iso_drv;
typedef struct vpart_iso_drv vpart_iso_drv_t;

enum {
  GUEST_STATE_UNINIT,
  GUEST_STATE_READYING,
  GUEST_STATE_READY,
  GUEST_STATE_WORKING,
  GUEST_STATE_STOPPING,
};

typedef struct guest_ctx {
  // ipc
  zx_handle_t fifo_in[VBLOCK_FIFO_COUNT];
  zx_handle_t fifo_out[VBLOCK_FIFO_COUNT];

  thrd_t worker_thread;
  uint32_t state;
  int vmid;

  // guest gpa vmo
  zx_handle_t gpa_vmo;
  void *(*translate_gpa_to_vaddr)(struct guest_ctx *, uint64_t);
  struct mem_region vm_phys_mem;
  void *mapped_vaddr;

  // request related
  size_t block_op_size;
  // request buffer
  void *requests_buffer;
  uint32_t free_next[FIFO_QDEPTH];
  uint32_t free_head;
  mtx_t free_lock;
  completion_t requests_completion;

  // crypto
  struct blk_crypto_profile *(*get_crypto_profile)(struct guest_ctx *);
  struct blk_crypto_key crypto_keys[BLK_CRYPTO_KEY_SLOT_NUM];
  unsigned long crypto_key_bitmap;

  // statistics
  uint64_t request_count;
  uint64_t completed_count;

  // virt partition isolate
  vpart_iso_drv_t *vpi_drv;
  bool has_partition_isolation;
} guest_ctx_t;

typedef struct direct_io_ctx {
  zx_handle_t gpa_vmo;
  zx_handle_t io_vmo;
  struct mem_region vm_phys_mem;
  void *mapped_vaddr;
  block_op_t *io_block_op;
  bool inited;
} direct_io_ctx_t;

typedef struct vblock_drv {
  guest_ctx_t guests[VBLOCK_MAX_GUESTS];
  direct_io_ctx_t direct_guest[VBLOCK_MAX_GUESTS];
  block_protocol_t *block_proto;
  block_info_t *block_info;
  size_t block_op_size;
  char backend_name[VBLOCK_BACKEND_NAME_LEN];
  mtx_t device_lock;
  int selected_guest_vmid;
  int selected_direct_vmid;
} vblock_drv_t;

zx_status_t vblock_dev_ioctl(vblock_drv_t *dev, uint32_t op, const void *in_buf,
                             size_t in_len, void *out_buf, size_t out_len,
                             size_t *out_actual);

void vblock_dev_release(vblock_drv_t *dev);

zx_status_t vblock_dev_set_backend(vblock_drv_t *dev, block_protocol_t *bp,
                                   block_info_t *info, size_t block_op_size,
                                   const char *backend_name);

void vblock_dev_init(vblock_drv_t *dev);
