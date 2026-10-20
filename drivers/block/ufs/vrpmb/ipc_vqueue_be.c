// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ipc_vqueue_be.h"

// aligned to 8
static size_t virtq_size(uint32_t virtq_depth) {
  return sizeof(struct virtio_common_zone) +
         sizeof(struct virtq_desc) * virtq_depth +
         (((sizeof(struct virtq_avail) + sizeof(uint16_t) * virtq_depth) + 7) &
          -8) +
         sizeof(struct virtq_used) + sizeof(uint16_t) +
         sizeof(struct virtq_used_elem) * virtq_depth;
}

void virtq_backend_to_used(struct virtqueue_backend *vq, uint16_t desc_idx) {
  struct virtq_used *used_ring = vq->used;
  uint16_t used_idx;

  used_idx = vq->last_used_idx++;
  used_ring->ring[used_idx & (vq->virtq_depth - 1)].id = desc_idx;

  virtio_mb();

  used_ring->idx = used_idx + 1;

  ++vq->newly_added;
  virtq_backend_notify(vq);
}

void virtq_backend_set_used_event(struct virtqueue_backend *vq) {
  vq->avail->used_event = vq->consumed;
  virtio_mb();
}

static void virtq_backend_kick(struct virtqueue_backend *vq) {
  zx_status_t status;

  virtio_mb();
  status = zx_object_signal_peer(vq->kick_event, 0, VIRTQ_EVENT_SIGNAL_1);
  if (status != ZX_OK) {
    printf("ipc, be_vq notify failed, error code: %d\n", status);
  }
}

bool virtq_backend_notify(struct virtqueue_backend *vq) {
  virtq_backend_kick(vq);
  vq->newly_added = 0;
  return true;
}

int virtq_backend_get_avail(struct virtqueue_backend *vq, void *buf,
                            uint16_t *head) {
  struct virtq_avail *avail_ring = vq->avail;
  struct virtq_desc *desc;
  uint16_t avail_idx;
  uint16_t desc_idx;

  if (!vq->running && !vq->common->initialized) {
    return -1;
  }

  if (vq->last_avail_idx == avail_ring->idx) {
    return -1;
  }

  avail_idx = vq->last_avail_idx++ & (vq->virtq_depth - 1);
  desc_idx = avail_ring->ring[avail_idx];

  desc = vq->desc;
  memcpy(buf, desc[desc_idx].data, sizeof(struct q_item));

  *head = desc_idx;
  ++vq->consumed;

  return 0;
}

int vq_backend_init(struct virtqueue_backend *vq, void *shm_addr,
                    size_t shm_size, uint32_t virtq_depth, zx_handle_t event) {
  size_t vq_size;
  size_t offset = 0;

  if (!vq || !shm_addr || virtq_depth == 0) {
    printf("ERROR: Invalid parameters for backend init\n");
    return -1;
  }

  vq_size = virtq_size(virtq_depth);
  printf("be, vq_size: 0x%lx\n", vq_size);
  if (vq_size > shm_size) {
    printf("ERROR: Out of shared memory\r\n");
    return -1;
  }

  if ((uint64_t)shm_addr & 7)
    printf("warning: ipc shm is not aligned to 8\n");

  printf("common_zone off: 0x%lx\n", offset);
  vq->common = (struct virtio_common_zone *)(shm_addr + offset);
  vq->common->magic = 0xdeadbeaf;
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
  vq->magic_end[0] = 0xdeadbeef;

  vq->shm_base = shm_addr;
  vq->virtq_depth = virtq_depth;

  vq->last_avail_idx = 0;
  vq->last_used_idx = 0;
  vq->consumed = 0;

  vq->avail->used_event = 0;
  vq->used->avail_event = 0;
  vq->avail->idx = 0;
  vq->used->idx = 0;

  mtx_init(&vq->lock, mtx_plain);

  vq->kick_event = event;
  vq->running = 1;
  vq->newly_added = 0;

  return 0;
}
