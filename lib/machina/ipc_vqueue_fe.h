// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>
#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DESC_NEXT_END 0xffff

#define VIRTQ_DESC_F_NEXT 0x1
#define VIRTQ_DESC_F_WRITE 0x2
#define VIRTQ_DESC_F_READ 0x0

#define VIRTQ_EVENT_THRESHOLD 0

#define VIRTQ_EVENT_HANDLE_0 0
#define VIRTQ_EVENT_HANDLE_1 1
#define VIRTQ_EVENT_SIGNAL_0 ZX_USER_SIGNAL_0
#define VIRTQ_EVENT_SIGNAL_1 ZX_USER_SIGNAL_1

#define barrier() ({ __asm__ __volatile__("" : : : "memory"); })

#define virtio_mb()                                                            \
  do {                                                                         \
    barrier();                                                                 \
    atomic_thread_fence(memory_order_seq_cst);                                 \
  } while (0)
#define virtio_wmb()                                                           \
  do {                                                                         \
    barrier();                                                                 \
    atomic_thread_fence(memory_order_release);                                 \
  } while (0)
#define virtio_rmb()                                                           \
  do {                                                                         \
    barrier();                                                                 \
    atomic_thread_fence(memory_order_acquire);                                 \
  } while (0)

struct virtio_common_zone {
  volatile uint32_t magic;
  volatile int initialized;
  volatile uint16_t virtq_depth;
  volatile size_t shm_size;
} __attribute__((aligned(8)));

struct q_item {
  volatile uint64_t data0;
  volatile uint64_t data1;
  volatile uint64_t reserved;
};

struct virtq_desc {
  union {
    uint8_t data[1];
    struct q_item itm[1];
  };
} __attribute__((aligned(8)));

struct virtq_avail {
  volatile uint16_t used_event;
  volatile uint16_t flags;
  volatile uint16_t idx;
  volatile uint16_t ring[];
};

struct virtq_used_elem {
  volatile uint32_t id;
  volatile uint32_t len;
};

struct vq_desc_extra {
  uint16_t next;
};

struct virtq_used {
  uint16_t pad;
  volatile uint16_t avail_event;
  volatile uint16_t flags;
  volatile uint16_t idx;
  struct virtq_used_elem ring[];
};

typedef void (*vq_completion_cb)(uint32_t request_id, void *user_ctx);

struct virtqueue_frontend {
  struct virtio_common_zone *common;
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  volatile uint32_t *magic_end;

  zx_handle_t event;

  void *shm_base;
  uint32_t virtq_depth;
  struct vq_desc_extra *extras;

  uint16_t free_head;
  uint16_t num_free;
  uint16_t avail_idx;

  uint16_t last_used_idx;

  uint16_t newly_added;

  uint32_t *request_ids;
  void **user_ctx;

  int running;

  mtx_t lock;
};

__BEGIN_CDECLS

int virtq_frontend_init(struct virtqueue_frontend *vq, void *shm_addr,
                        size_t shm_size, uint32_t virtq_depth,
                        zx_handle_t event);

int virtq_frontend_to_avail(struct virtqueue_frontend *vq, const void *data,
                            uint32_t data_len, uint32_t req_id, void *user_ctx);

int virtq_frontend_get_used(struct virtqueue_frontend *vq, void *buf,
                            uint32_t *req_id, void **user_ctx);
void virtq_frontend_set_used_event(struct virtqueue_frontend *vq);

int vq_frontend_wait_event(struct virtqueue_frontend *vq, bool);

__END_CDECLS
