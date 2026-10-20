// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ipc_vqueue_fe.h"

// aligned to 8
static size_t virtq_size(uint32_t virtq_depth) {
  return sizeof(struct virtio_common_zone) +
         sizeof(struct virtq_desc) * virtq_depth +
         (((sizeof(struct virtq_avail) + sizeof(uint16_t) * virtq_depth) + 7) &
          -8) +
         sizeof(struct virtq_used) + sizeof(uint16_t) +
         sizeof(struct virtq_used_elem) * virtq_depth;
}

void virtq_frontend_set_used_event(struct virtqueue_frontend *vq) {
  vq->used->avail_event = vq->last_used_idx;
  virtio_mb();
}

int virtq_frontend_get_used(struct virtqueue_frontend *vq, void *buf,
                            uint32_t *req_id, void **user_ctx) {
  struct virtq_used_elem *used_elem;
  struct virtq_used *used_ring = vq->used;
  uint16_t used_idx;
  uint16_t desc_idx;

  if (unlikely(!vq || !user_ctx)) {
    printf("ERROR: Invalid parameters for frontend get used\r\n");
    return -1;
  }

  if (unlikely(!vq->running)) {
    printf("ERROR: ipc_vq is not running\r\n");
    return -1;
  }

  if (vq->last_used_idx == used_ring->idx) {
    return -1;
  }

  used_idx = vq->last_used_idx++ & (vq->virtq_depth - 1);
  used_elem = &used_ring->ring[used_idx];

  desc_idx = used_elem->id;

  if (unlikely(desc_idx >= vq->virtq_depth)) {
    printf("ipc vq_fe err: desc_idx > vq depth: desc_idx: %u\n", desc_idx);
    return -1;
  }

  *req_id = vq->request_ids[desc_idx];
  *user_ctx = vq->user_ctx[desc_idx];

  if (buf)
    memcpy(buf, vq->desc[desc_idx].data, sizeof(struct q_item));

  virtio_mb();

  mtx_lock(&vq->lock);
  vq->extras[desc_idx].next = vq->free_head;
  vq->free_head = desc_idx;
  vq->num_free++;
  mtx_unlock(&vq->lock);

  return 0;
}

int vq_frontend_wait_event(struct virtqueue_frontend *vq, bool reset) {
  zx_signals_t observed = 0;
  zx_status_t status = zx_object_wait_one(vq->event, VIRTQ_EVENT_SIGNAL_1,
                                          ZX_TIME_INFINITE, &observed);
  if (status == ZX_OK) {
    if (!(observed & VIRTQ_EVENT_SIGNAL_1))
      printf("ipc vq_fe warning: received unexpected sig: 0x%x\n", observed);

    if (reset)
      zx_object_signal(vq->event, VIRTQ_EVENT_SIGNAL_1, 0);

    return 0;
  }
  return -1;
}

static void vq_frontend_kick(zx_handle_t event) {
  zx_status_t status;

  virtio_mb();
  status = zx_object_signal_peer(event, 0, ZX_EVENT_SIGNALED);
  if (status != ZX_OK) {
    printf(
        "Sending end signal setting failed, ZX_USER_SIGNAL_0, error code: %d\n",
        status);
  }
}

static void vq_frontend_should_notify(struct virtqueue_frontend *vq) {
  vq_frontend_kick(vq->event);
}

int virtq_frontend_to_avail(struct virtqueue_frontend *vq, const void *data,
                            uint32_t data_len, uint32_t req_id,
                            void *user_ctx) {
  uint16_t desc_idx;
  uint16_t avail_idx;

  struct virtq_desc *desc;
  struct virtq_avail *avail_ring;

  if (!vq || !data || data_len == 0) {
    return -1;
  }

  if (vq->num_free <= 0) {
    printf("vq->num_free == 0");
    return -1;
  }

  desc = vq->desc;

  mtx_lock(&vq->lock);

  desc_idx = vq->free_head;
  vq->free_head = vq->extras[desc_idx].next;
  vq->num_free--;
  mtx_unlock(&vq->lock);

  vq->request_ids[desc_idx] = req_id;
  vq->user_ctx[desc_idx] = user_ctx;

  memcpy(desc[desc_idx].data, data, sizeof(struct q_item));
  struct q_item const *xbuf = (void *)desc[desc_idx].data;
  if (0 == xbuf->data0)
    printf("fuck ipc fe vq paddr == 0\n");

  avail_idx = vq->avail_idx++;

  avail_ring = vq->avail;
  avail_ring->ring[avail_idx & (vq->virtq_depth - 1)] = desc_idx;

  virtio_mb();

  avail_ring->idx = vq->avail_idx;

  ++vq->newly_added;

  vq_frontend_should_notify(vq);
  return 0;
}

int virtq_frontend_init(struct virtqueue_frontend *vq, void *shm_addr,
                        size_t shm_size, uint32_t virtq_depth,
                        zx_handle_t event) {
  size_t offset = 0;
  size_t vq_size;
  uint16_t i;

  if (!vq || !shm_addr || virtq_depth == 0) {
    printf("ERROR: Invalid parameters for frontend init\r\n");
    return -1;
  }

  vq_size = virtq_size(virtq_depth);
  printf("fe, vq_size: 0x%lx\n", vq_size);
  if (vq_size > shm_size) {
    printf("ERROR: Out of shared memory\r\n");
    return -1;
  }

  memset(vq, 0, sizeof(*vq));

  vq->shm_base = shm_addr;
  vq->virtq_depth = virtq_depth;

  vq->request_ids = calloc(virtq_depth, sizeof(uint32_t));
  vq->user_ctx = calloc(virtq_depth, sizeof(void *));
  vq->extras = calloc(virtq_depth, sizeof(struct vq_desc_extra));
  if (!vq->request_ids || !vq->user_ctx || !vq->extras) {
    printf("ERROR: ipc vq Failed to allocate arrays\r\n");
    free(vq->request_ids);
    free(vq->user_ctx);
    free(vq->extras);
    return -1;
  }

  if ((uint64_t)shm_addr & 7)
    printf("warning: ipc shm is not aligned to 8\n");

  printf("common_zone off: 0x%lx\n", offset);
  vq->common = (struct virtio_common_zone *)(shm_addr + offset);
  printf("ipc vq fe, common.magic: 0x%x\n", vq->common->magic);

  offset += sizeof(struct virtio_common_zone);

  printf("desc off: 0x%lx\n", offset);
  vq->desc = (struct virtq_desc *)(shm_addr + offset);
  offset += sizeof(struct virtq_desc) * virtq_depth;

  printf("avail off: 0x%lx\n", offset);
  vq->avail = (struct virtq_avail *)(shm_addr + offset);
  offset +=
      (sizeof(struct virtq_avail) + sizeof(uint16_t) * virtq_depth + 7) & -8;

  printf("used off: 0x%lx\n", offset);
  vq->used = (struct virtq_used *)(shm_addr + offset);
  offset += (sizeof(struct virtq_used) +
             sizeof(struct virtq_used_elem) * virtq_depth);
  vq->magic_end = shm_addr + offset;
  printf("ipc vq fe, magic_end: 0x%x\n", vq->magic_end[0]);
  vq->event = event;

  vq->num_free = virtq_depth;
  vq->newly_added = 0;

  vq->last_used_idx = 0;
  vq->avail_idx = 0;

  vq->running = 1;

  for (i = 0; i < virtq_depth - 1; i++)
    vq->extras[i].next = i + 1;
  vq->extras[virtq_depth - 1].next = DESC_NEXT_END;
  vq->free_head = 0;

  mtx_init(&vq->lock, mtx_plain);

  return 0;
}
