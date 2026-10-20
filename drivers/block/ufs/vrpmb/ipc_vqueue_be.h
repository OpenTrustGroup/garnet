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
#include <pthread.h>

#define DESC_NEXT_END 0xffff

#define VIRTQ_DESC_F_NEXT 0x1
#define VIRTQ_DESC_F_WRITE 0x2
#define VIRTQ_DESC_F_READ 0x0

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

#define VIRTQ_EVENT_HANDLE_0 0
#define VIRTQ_EVENT_HANDLE_1 1
#define VIRTQ_EVENT_SIGNAL_0 ZX_USER_SIGNAL_0
#define VIRTQ_EVENT_SIGNAL_1 ZX_USER_SIGNAL_1

#define VIRTQ_EVENT_THRESHOLD 0

//#define DESC_ADDR_SIZE		16

#define __maybe_unused __attribute__((unused))

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

struct virtq_used {
  uint16_t pad;
  volatile uint16_t avail_event;
  volatile uint16_t flags;
  volatile uint16_t idx;
  struct virtq_used_elem ring[];
};

struct virtqueue_backend {
  struct virtio_common_zone *common;
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  volatile uint32_t *magic_end;

  void *shm_base;
  uint32_t virtq_depth;

  uint16_t last_avail_idx;
  uint16_t last_used_idx;
  uint16_t consumed;
  int running;

  pthread_t poll_thread;
  uint16_t newly_added;

  mtx_t lock;
  zx_handle_t kick_event;
};

__BEGIN_CDECLS

int vq_backend_init(struct virtqueue_backend *vq, void *shm_addr,
                    size_t shm_size, uint32_t virtq_depth,
                    zx_handle_t kick_event);
int virtq_backend_get_avail(struct virtqueue_backend *vq, void *buf,
                            uint16_t *head);
void virtq_backend_to_used(struct virtqueue_backend *vq, uint16_t desc_idx);
bool virtq_backend_notify(struct virtqueue_backend *vq);
void virtq_backend_set_used_event(struct virtqueue_backend *vq);

__END_CDECLS
