// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <virtio/virtio_ring.h>
#include <trusty_std.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "vblock-drv.h"
#include "vblock-drv-crypto.h"
#include "../blk_crypto.h"
#include "blk_retry/retry.h"
#include "vpart_isolate.h"

#define SECTOR_BITS (9)
#define MAX(a, b) ((a > b) ? a : b)

#define VBLOCK_DRV_THREAD_PRIORITY 17
#define PERF_BIND_CPU_TEMPORARILY 1

#define VBLOCK_HANDLE_TYPE_MASK 0xf0
#define VBLOCK_HANDLE_TYPE_SHM 0x00
#define VBLOCK_HANDLE_TYPE_EVENT 0x10
#define VBLOCK_HANDLE_TYPE_FIFO 0x20
#define VBLOCK_HANDLE_INDEX_MASK 0x0f

#define VBLOCK_WORKER_CPU_AFFINITY_MASK 0xf0
#define VBLOCK_MAX_THREAD_NAME_LEN 32
#define VBLOCK_DEFAULT_CACHE_LINE_SIZE 64

#define VBLOCK_FLUSH_WAIT_MSEC 100

static bool vblock_backend_needs_partition_isolation(vblock_drv_t *dev) {
  return strncmp(dev->backend_name, "sdc", strlen("sdc")) == 0;
}
#define VBLOCK_STATE_WAIT_MSEC 1
#define VBLOCK_STATE_STOP_WAIT_MSEC 5

#define VBLOCK_FIFO_BATCH_SIZE FIFO_QDEPTH

// a req mem layout:
// | .block_op_t. | .block driver inner use. | .pad. | .vblock_drv_cb. | .pad. |
// |
// |<---------   bop.block_op_size   ------->|
// |<--------------- drv.block_op_size ------------->|
// |<---------------------------- gst.block_op_size -------------------------->|

#define SET_STATE(g, s) __atomic_store_n(&(g)->state, s, __ATOMIC_RELEASE)
#define GET_STATE(g) __atomic_load_n(&(g)->state, __ATOMIC_ACQUIRE)

typedef struct {
  completion_t completion;
  zx_status_t status;
} read_ctx_t;

struct direct_read_param {
  int32_t vmid;
  struct direct_io_rw_param rw;
};

typedef struct vblock_drv_cb {
  uint64_t phy_status;
  uint32_t head;
  uint16_t idx;
  retry_ctx_t *retry_ctx;
  void *crypto_ctx_alloc;
} vblock_drv_cb_t;

/* Discard/write zeroes range for each request. */
struct virtio_blk_discard_write_zeroes {
  /* discard/write zeroes start sector */
  uint64_t sector;
  /* number of discard/write zeroes sectors */
  uint32_t num_sectors;
  /* flags for this range */
  uint32_t flags;
};

static uint64_t vmo_get_size(zx_handle_t vmo) {
  uint64_t size;
  if (zx_vmo_get_size(vmo, &size) == ZX_OK)
    return size;
  return UINT64_MAX;
}

#define to_vblock_dev(guest)                                                   \
  container_of(guest, vblock_drv_t, guests[guest->vmid])

static void guest_ctx_reset(guest_ctx_t *guest) {
  guest->request_count = 0;
  guest->completed_count = 0;
  guest->worker_thread = 0;
  guest->gpa_vmo = ZX_HANDLE_INVALID;
  guest->mapped_vaddr = NULL;

  for (int i = 0; i < VBLOCK_FIFO_COUNT; i++) {
    guest->fifo_in[i] = ZX_HANDLE_INVALID;
    guest->fifo_out[i] = ZX_HANDLE_INVALID;
  }

  SET_STATE(guest, GUEST_STATE_UNINIT);

  guest->crypto_key_bitmap = 0;
  for (int i = 0; i < BLK_CRYPTO_KEY_SLOT_NUM; ++i)
    blk_crypto_key_set_priv(guest->crypto_keys + i,
                            (uint8_t)(guest->vmid & 0xff));

  guest->free_head = 0;
  for (int i = 0; i < FIFO_QDEPTH - 1; ++i) {
    guest->free_next[i] = i + 1;
  }
  guest->free_next[FIFO_QDEPTH - 1] = UINT32_MAX;
  completion_reset(&guest->requests_completion);

  guest->vpi_drv = NULL;
  guest->has_partition_isolation = false;
}

static void guest_ctx_init(guest_ctx_t *guest) {
  mtx_init(&guest->free_lock, mtx_plain);
  guest_ctx_reset(guest);
}

static void guest_ctx_exit(guest_ctx_t *guest) {
  uint32_t expected = GET_STATE(guest);
  if (expected == GUEST_STATE_UNINIT) {
    return;
  }

  if (expected != GUEST_STATE_STOPPING) {
    SET_STATE(guest, GUEST_STATE_STOPPING);
  }

  // Wake up worker thread blocked in guest_get_request
  completion_signal(&guest->requests_completion);

  // Close in FIFO early to forcefully wake up blocked worker thread
  if (guest->fifo_in[VBLOCK_FIFO_END_SELF] != ZX_HANDLE_INVALID) {
    zx_handle_close(guest->fifo_in[VBLOCK_FIFO_END_SELF]);
    guest->fifo_in[VBLOCK_FIFO_END_SELF] = ZX_HANDLE_INVALID;
  }

  // Ensure worker thread is safely terminated
  if (guest->worker_thread != 0) {
    thrd_join(guest->worker_thread, NULL);
    guest->worker_thread = 0;
    zxlogf(INFO, "[vblock][%d]: worker thread joined\n", guest->vmid);
  }

  // Wait for inflight I/O requests finished to prevent UAF
  while (__atomic_load_n(&guest->request_count, __ATOMIC_RELAXED) >
         __atomic_load_n(&guest->completed_count, __ATOMIC_RELAXED)) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(VBLOCK_FLUSH_WAIT_MSEC)));
  }
  atomic_thread_fence(memory_order_seq_cst);

  // Close out FIFO before freeing resources
  if (guest->fifo_out[VBLOCK_FIFO_END_SELF] != ZX_HANDLE_INVALID) {
    zx_handle_close(guest->fifo_out[VBLOCK_FIFO_END_SELF]);
    guest->fifo_out[VBLOCK_FIFO_END_SELF] = ZX_HANDLE_INVALID;
  }

  // If peer handles were never transferred out, close them here as well.
  if (guest->fifo_in[VBLOCK_FIFO_END_PEER] != ZX_HANDLE_INVALID) {
    zx_handle_close(guest->fifo_in[VBLOCK_FIFO_END_PEER]);
    guest->fifo_in[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
  }
  if (guest->fifo_out[VBLOCK_FIFO_END_PEER] != ZX_HANDLE_INVALID) {
    zx_handle_close(guest->fifo_out[VBLOCK_FIFO_END_PEER]);
    guest->fifo_out[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
  }

  // Evict all crypto keys
  virtio_blk_evict_all_keys((guest_ctx_t *)guest);

  if (guest->vpi_drv) {
    vpart_isolate_release(guest);
  }

  if (guest->gpa_vmo != ZX_HANDLE_INVALID) {
    if (guest->mapped_vaddr) {
      uint64_t vmar_sz = mem_region_size(&guest->vm_phys_mem);
      if (vmar_sz > 0) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)guest->mapped_vaddr,
                      vmar_sz);
      }
    }
    zx_handle_close(guest->gpa_vmo);
    guest->gpa_vmo = ZX_HANDLE_INVALID;
  }

  if (guest->requests_buffer) {
    uint8_t *req_base = (uint8_t *)guest->requests_buffer;
    vblock_drv_t *dev = to_vblock_dev(guest);
    for (int i = 0; i < FIFO_QDEPTH; ++i) {
      void *req_ptr = req_base + guest->block_op_size * i;
      vblock_drv_cb_t *cb =
          (vblock_drv_cb_t *)((uint8_t *)req_ptr + dev->block_op_size);
      if (cb->crypto_ctx_alloc) {
        free(cb->crypto_ctx_alloc);
        cb->crypto_ctx_alloc = NULL;
      }
    }
    free(guest->requests_buffer);
    guest->requests_buffer = NULL;
  }

  guest_ctx_reset(guest);
}

static inline int get_guest_vmid(vblock_drv_t *dev, guest_ctx_t *guest) {
  return guest - dev->guests;
}

static void guest_put_request(guest_ctx_t *guest, vblock_drv_cb_t *cb) {
  mtx_lock(&guest->free_lock);
  guest->free_next[cb->idx] = guest->free_head;
  guest->free_head = cb->idx;
  mtx_unlock(&guest->free_lock);
  completion_signal(&guest->requests_completion);
}

static void *guest_get_request(vblock_drv_t *dev, guest_ctx_t *guest,
                               vblock_drv_cb_t **cb) {
  zx_status_t status;

  while (true) {
    // Check stopping state to prevent deadlock
    if (unlikely(GET_STATE(guest) >= GUEST_STATE_STOPPING)) {
      return NULL;
    }

    mtx_lock(&guest->free_lock);
    uint32_t head_idx = guest->free_head;

    if (head_idx != UINT32_MAX) {
      guest->free_head = guest->free_next[head_idx];
      mtx_unlock(&guest->free_lock);

      uint8_t *req_base = (uint8_t *)guest->requests_buffer;
      void *res = req_base + guest->block_op_size * head_idx;
      *cb = (vblock_drv_cb_t *)((uint8_t *)res + dev->block_op_size);
      return res;
    }
    mtx_unlock(&guest->free_lock);

    status = completion_wait(&guest->requests_completion, ZX_TIME_INFINITE);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[vblock][%d]: wait for free requests_completion failed\n",
             guest->vmid);
      return NULL;
    }

    // Prevent resetting completion if stopping
    if (unlikely(GET_STATE(guest) >= GUEST_STATE_STOPPING)) {
      return NULL;
    }

    completion_reset(&guest->requests_completion);
  }
}

static inline zx_status_t
write_back_status(guest_ctx_t *guest, vblock_drv_cb_t *cb, uint8_t status) {
  if (cb->phy_status == UINT64_MAX) {
    zxlogf(ERROR, "[vblock][%d]: pstatus not resolved, skip write back\n",
           guest->vmid);
    return ZX_ERR_BAD_STATE;
  }

  uint8_t *resp =
      (uint8_t *)guest->translate_gpa_to_vaddr(guest, cb->phy_status);
  if (!resp) {
    zxlogf(ERROR, "[vblock][%d]: pstatus, out of range, pa: 0x%lx\n",
           guest->vmid, cb->phy_status);
    return ZX_ERR_OUT_OF_RANGE;
  }
  *resp = status;
  return ZX_OK;
}

static inline void push_cq(guest_ctx_t *guest, vblock_drv_cb_t *cb,
                           bool has_error) {
  uint32_t head = cb->head;
  struct fifo_out_item item;
  item.head = head;
  uint32_t actual = 0;
  zx_status_t status;

  uint64_t current_count =
      __atomic_fetch_add(&guest->completed_count, 1, __ATOMIC_RELAXED) + 1;

  guest_put_request(guest, cb);

  if (unlikely(has_error))
    zxlogf(TRACE, "[vblock][%d]: push cq error trace, head: %u, cnt: %lu\n",
           guest->vmid, head, current_count);

  do {
    status = zx_fifo_write_old(guest->fifo_out[VBLOCK_FIFO_END_SELF], &item,
                               sizeof(item), &actual);

    if (status == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      if ((status = zx_object_wait_one(guest->fifo_out[VBLOCK_FIFO_END_SELF],
                                       ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                                       ZX_TIME_INFINITE, &signals)) != ZX_OK) {
        zxlogf(ERROR, "[vblock][%d]: wait write fifo not OK: %d\n", guest->vmid,
               status);
        break;
      } else if (signals & ZX_FIFO_PEER_CLOSED) {
        zxlogf(ERROR, "[vblock][%d]: wait write fifo peer closed: %d\n",
               guest->vmid, status);
        break;
      }
      continue;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "[vblock][%d]: write fifo not OK: %d\n", guest->vmid,
             status);
      break;
    }
    break;
  } while (1);
}

static zx_status_t process_request_cb_internal(guest_ctx_t *guest,
                                               vblock_drv_cb_t *cb,
                                               zx_status_t ddk_status) {
  uint8_t virtio_status;

  switch (ddk_status) {
  case ZX_ERR_NOT_SUPPORTED:
    virtio_status = VIRTIO_BLK_S_UNSUPP;
    break;
  case ZX_OK:
    virtio_status = VIRTIO_BLK_S_OK;
    break;
  default:
    virtio_status = VIRTIO_BLK_S_IOERR;
  }

  zx_status_t wb_status = write_back_status(guest, cb, virtio_status);
  push_cq(guest, cb, wb_status != ZX_OK);
  return wb_status;
}

static void process_request_cb(block_op_t *block_op, zx_status_t status) {
  guest_ctx_t *guest = block_op->cookie;
  vblock_drv_t *dev = to_vblock_dev(guest);
  vblock_drv_cb_t *cb =
      (vblock_drv_cb_t *)((uint8_t *)block_op + dev->block_op_size);

  if (perform_retry_diagnosis(&cb->retry_ctx, block_op, status,
                              dev->block_proto->ops, dev->block_proto->ctx,
                              RETRY_FIXED_DELAY)) {
    return;
  }
  block_retry_free(cb->retry_ctx);

  if (block_op_crypto_ctx(block_op)) {
    blk_crypto_put_keyslot(block_op_crypto_keyslot(block_op));
    block_op_crypto_ctx(block_op) = NULL;
  }

  if (unlikely(process_request_cb_internal(guest, cb, status) != ZX_OK))
    dump_bop(block_op);
}

static zx_status_t analyze_rw_req(vblock_drv_t *dev, guest_ctx_t *guest,
                                  const req_hdr_t *hdr,
                                  const struct vring_desc *desc,
                                  block_op_t *block_op) {
  uint64_t vmo_off = desc->addr - guest->vm_phys_mem.start;
  uint64_t len = desc->len;

  block_op->rw.vmo = guest->gpa_vmo;
  block_op->rw.pages = guest->mapped_vaddr;
  // Legacy virtio requests have one data descriptor.  Clear the native-SG
  // marker because this block_op is recycled and UFS treats sg_count > 0 as
  // a native-SG request.
  block_op->rw.sg_count = 0;

  block_op->rw.sectors = len >> SECTOR_BITS;
  block_op->rw.length = len / dev->block_info->block_size;

  block_op->rw.sector = hdr->sector;
  block_op->rw.offset_dev =
      (hdr->sector << SECTOR_BITS) / dev->block_info->block_size;

  block_op->rw.vm = desc->addr;
  block_op->rw.vmo_off = vmo_off;
  block_op->rw.offset_vmo = vmo_off / dev->block_info->block_size;

  if (hdr->use_crypto) {
    vblock_drv_cb_t *cb =
        (vblock_drv_cb_t *)((uint8_t *)block_op + dev->block_op_size);
    bop_set_crypto(guest, block_op, hdr, cb->crypto_ctx_alloc);
  } else {
    block_op_crypto_ctx(block_op) = NULL;
  }

  return ZX_OK;
}

/* discard/write-zeroes/secure-erase request */
static zx_status_t
analyze_discard_writezeroes_req(vblock_drv_t *dev, guest_ctx_t *guest,
                                const struct vring_desc *desc,
                                block_op_t *block_op) {
  const struct virtio_blk_discard_write_zeroes *param;
  uint32_t alignment = dev->block_info->block_size;

  if (unlikely(desc->len != sizeof(struct virtio_blk_discard_write_zeroes))) {
    zxlogf(ERROR, "[vblock][%d]: invalid desc->len:%d\n", guest->vmid,
           desc->len);
    return ZX_ERR_INVALID_ARGS;
  }

  if (block_op->command == BLOCK_OP_DISCARD)
    alignment =
        MAX(dev->block_info->discard_granularity, dev->block_info->block_size);
  else if (block_op->command == BLOCK_OP_SECURE_ERASE)
    alignment = MAX(dev->block_info->secure_erase_granularity,
                    dev->block_info->block_size);

  param = (const struct virtio_blk_discard_write_zeroes *)
              guest->translate_gpa_to_vaddr(guest, desc->addr);
  if (unlikely(!param)) {
    zxlogf(ERROR, "[vblock][%d]: translate_gpa_to_vaddr failed for discard\n",
           guest->vmid);
    return ZX_ERR_OUT_OF_RANGE;
  }

  block_op->erase.sector = param->sector;
  block_op->erase.sectors = param->num_sectors;
  block_op->erase.length =
      (param->num_sectors << SECTOR_BITS) / dev->block_info->block_size;
  block_op->erase.offset_dev =
      (param->sector << SECTOR_BITS) / dev->block_info->block_size;
  block_op->erase.flags = param->flags;

  // TODO: crypto

  return ZX_OK;
}

static zx_status_t vblock_dev_handle_request(guest_ctx_t *guest,
                                             const struct fifo_in_item *item) {
  vblock_drv_t *dev = to_vblock_dev(guest);
  uint16_t head = (uint16_t)item->head;
  vblock_drv_cb_t *cb;
  zx_status_t status = ZX_OK;
  bool rgpt_flag = false;

  block_op_t *block_op = guest_get_request(dev, guest, &cb);
  if (unlikely(!block_op)) {
    __atomic_fetch_sub(&guest->request_count, 1, __ATOMIC_RELAXED);
    return ZX_ERR_NO_RESOURCES;
  }

  cb->head = head;
  cb->phy_status = UINT64_MAX;

  const struct vring_desc *desc_lv2 =
      guest->translate_gpa_to_vaddr(guest, item->addr);
  if (unlikely(!desc_lv2)) {
    zxlogf(ERROR, "[vblock][%d]: head: %u, desc_lv2, out of range, pa: 0x%lx\n",
           guest->vmid, head, item->addr);
    status = VIRTIO_BLK_S_IOERR;
    goto quick_complete;
  }

  int desc_cnt = item->len;
  if (unlikely(desc_cnt < 2 || desc_cnt > FIFO_QDEPTH)) {
    zxlogf(ERROR,
           "[vblock][%d]: error descs, invalid desc_cnt: %u, lv2_pa: 0x%lx\n",
           guest->vmid, desc_cnt, item->addr);
    status = VIRTIO_BLK_S_IOERR;
    goto quick_complete;
  }

  cb->phy_status = (desc_lv2 + desc_cnt - 1)->addr;

  const req_hdr_t *hdr = guest->translate_gpa_to_vaddr(guest, desc_lv2->addr);
  if (unlikely(!hdr)) {
    zxlogf(ERROR,
           "[vblock][%d]: hdr, out of range, pa: 0x%lx, lv2_pa: 0x%lx, "
           "desc_cnt: %u\n",
           guest->vmid, desc_lv2->addr, item->addr, desc_cnt);
    status = VIRTIO_BLK_S_IOERR;
    goto quick_complete;
  }

  switch (hdr->type) {
  case VIRTIO_BLK_T_IN:
    block_op->command = BLOCK_OP_READ;
    break;
  case VIRTIO_BLK_T_OUT:
    block_op->command = BLOCK_OP_WRITE;
    break;
  case VIRTIO_BLK_T_FLUSH:
    block_op->command = BLOCK_OP_FLUSH;
    break;
  case VIRTIO_BLK_T_DISCARD:
    block_op->command = BLOCK_OP_DISCARD;
    break;
  case VIRTIO_BLK_T_WRITE_ZEROES:
    block_op->command = BLOCK_OP_WRITE_ZEROES;
    break;
  case VIRTIO_BLK_T_SECURE_ERASE:
    block_op->command = BLOCK_OP_SECURE_ERASE;
    break;
  default:
    status = VIRTIO_BLK_S_UNSUPP;
  }

  if (unlikely(status != ZX_OK))
    goto quick_complete;

  if (block_op->command == BLOCK_OP_READ ||
      block_op->command == BLOCK_OP_WRITE) {
    // Legacy vblock-drv has a single descriptor data path.  The device
    // advertises seg_max=1, but fail safely if a frontend violates it instead
    // of silently submitting only desc_lv2[1].
    if (unlikely(desc_cnt != 3)) {
      zxlogf(ERROR,
             "[vblock][%d]: legacy rw needs header/data/status, got %d descs\n",
             guest->vmid, desc_cnt);
      status = VIRTIO_BLK_S_IOERR;
      goto quick_complete;
    }
    status = analyze_rw_req(dev, guest, hdr, desc_lv2 + 1, block_op);
    if (unlikely(status != ZX_OK))
      goto quick_complete;
    block_op->rw.cache_flushed = true;
  } else if (block_op->command == BLOCK_OP_DISCARD ||
             block_op->command == BLOCK_OP_WRITE_ZEROES ||
             block_op->command == BLOCK_OP_SECURE_ERASE) {
    status =
        analyze_discard_writezeroes_req(dev, guest, desc_lv2 + 1, block_op);
    if (unlikely(status != ZX_OK))
      goto quick_complete;
  }
  cb->retry_ctx = NULL;
  block_op->cookie = guest;
  block_op->completion_cb = process_request_cb;

  if ((block_op->command != BLOCK_OP_FLUSH) &&
      (guest->has_partition_isolation)) {
    status =
        vpart_isolate_process_bop((guest_ctx_t *)guest, block_op, &rgpt_flag);
    if (unlikely(status != ZX_OK))
      goto quick_complete;

    if (rgpt_flag) {
      process_request_cb(block_op, status);
      return ZX_OK;
    }
  }
  dev->block_proto->ops->queue(dev->block_proto->ctx, block_op);

  return ZX_OK;

quick_complete:
  if (process_request_cb_internal(guest, cb, status) != ZX_OK)
    dump_bop(block_op);

  zxlogf(TRACE, "[vblock]: quick complete end\n");
  return ZX_OK;
}

static int guest_worker_thread(void *arg) {
  guest_ctx_t *guest = arg;
  zx_status_t status;
#if PERF_BIND_CPU_TEMPORARILY
  uint32_t cpu_mask = VBLOCK_WORKER_CPU_AFFINITY_MASK;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                (void *)&cpu_mask);
#endif
  zx_thread_set_priority(VBLOCK_DRV_THREAD_PRIORITY);

  SET_STATE(guest, GUEST_STATE_WORKING);

  while (true) {
    if (GET_STATE(guest) >= GUEST_STATE_STOPPING) {
      break;
    }

    struct fifo_in_item items[VBLOCK_FIFO_BATCH_SIZE];
    uint32_t actual = 0;

    status = zx_fifo_read_old(guest->fifo_in[VBLOCK_FIFO_END_SELF], items,
                              sizeof(items), &actual);
    if (status == ZX_OK) {
      if (actual > 0) {
        __atomic_fetch_add(&guest->request_count, actual, __ATOMIC_RELAXED);
        for (uint32_t i = 0; i < actual; i++) {
          vblock_dev_handle_request(guest, &items[i]);
        }
      }
    } else if (status == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      status = zx_object_wait_one(guest->fifo_in[VBLOCK_FIFO_END_SELF],
                                  ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                  ZX_TIME_INFINITE, &signals);
      if (status != ZX_OK) {
        zxlogf(ERROR, "[vblock][%d]: wait fifo read not OK: %d\n", guest->vmid,
               status);
        break;
      } else if (signals & ZX_FIFO_PEER_CLOSED) {
        zxlogf(ERROR, "[vblock][%d]: wait fifo read, peer closed: %d\n",
               guest->vmid, status);
        break;
      }
    } else {
      // fifo peer closed or other errors, just quit
      break;
    }
  }

  SET_STATE(guest, GUEST_STATE_STOPPING);
  zxlogf(INFO, "[vblock][%d]: guest worker quit: %d\n", guest->vmid, status);

  return ZX_OK;
}

static void *guest_gpa_to_vaddr(guest_ctx_t *guest, uint64_t gpa) {
  uint64_t pa = gpa;
  uint64_t offset = pa - guest->vm_phys_mem.start;

  if (unlikely(offset >= mem_region_size(&guest->vm_phys_mem)))
    return NULL;
  return (uint8_t *)guest->mapped_vaddr + offset;
}

static struct blk_crypto_profile *
vblock_dev_crypto_profile(guest_ctx_t *guest) {
  vblock_drv_t *dev = to_vblock_dev(guest);
  return dev->block_proto->ops->crypto_profile;
}

static zx_status_t vblock_guest_alloc_resources(guest_ctx_t *guest,
                                                struct mem_region *mem) {
  vblock_drv_t *dev = to_vblock_dev(guest);
  block_op_t dummy_block_op;

  guest->translate_gpa_to_vaddr = guest_gpa_to_vaddr;
  guest->block_op_size =
      (sizeof(vblock_drv_cb_t) + dev->block_op_size + 7) & -8;

  zxlogf(INFO, "[vblock][%d]: guest->block_op_size: %lu\n", guest->vmid,
         guest->block_op_size);

  guest->requests_buffer = calloc(FIFO_QDEPTH, guest->block_op_size);
  if (!guest->requests_buffer) {
    zxlogf(ERROR, "[vblock][%d]: failed to pre alloc request(block_op_t)\n",
           guest->vmid);
    return ZX_ERR_NO_MEMORY;
  }

  uint8_t *req_base = (uint8_t *)guest->requests_buffer;
  for (int i = 0; i < FIFO_QDEPTH; ++i) {
    void *req_ptr = req_base + guest->block_op_size * i;
    vblock_drv_cb_t *cb =
        (vblock_drv_cb_t *)((uint8_t *)req_ptr + dev->block_op_size);
    cb->idx = i;
    cb->crypto_ctx_alloc =
        calloc(1, sizeof(block_op_crypto_ctx((&dummy_block_op))[0]));

    if (!cb->crypto_ctx_alloc) {
      for (int j = 0; j < i; ++j) {
        void *prev_ptr = req_base + guest->block_op_size * j;
        vblock_drv_cb_t *prev_cb =
            (vblock_drv_cb_t *)((uint8_t *)prev_ptr + dev->block_op_size);
        free(prev_cb->crypto_ctx_alloc);
      }
      free(guest->requests_buffer);
      guest->requests_buffer = NULL;
      return ZX_ERR_NO_MEMORY;
    }
  }

  guest->vm_phys_mem = *mem;
  guest->get_crypto_profile = vblock_dev_crypto_profile;

  return ZX_OK;
}

static zx_status_t vblock_guest_start_thread(guest_ctx_t *guest) {
  char buf[VBLOCK_MAX_THREAD_NAME_LEN];
  snprintf(buf, VBLOCK_MAX_THREAD_NAME_LEN, "vblk-drv@%u", guest->vmid);

  zx_status_t status = thrd_create_with_name(&guest->worker_thread,
                                             guest_worker_thread, guest, buf);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock][%d]: failed to create vblk drv worker\n",
           guest->vmid);
    guest->worker_thread = 0;
    return status;
  }
  return ZX_OK;
}

static zx_status_t calculate_vmid(const void *buf, struct mem_region *_mem,
                                  size_t in_len, int *out_vmid) {
  const struct start_param {
    uint16_t vmid;
    struct mem_region mem;
  } *ptr = buf;

  if (sizeof(*ptr) != in_len) {
    zxlogf(ERROR,
           "[vblock]: guest start invalid args, struct len: %lu, in_len: %lu\n",
           sizeof(*ptr), in_len);
    return ZX_ERR_INVALID_ARGS;
  }

  if (_mem)
    *_mem = ptr->mem;
  *out_vmid = (int)ptr->vmid;
  return ZX_OK;
}

static zx_status_t vblock_guest_start(vblock_drv_t *dev, const void *in_buf,
                                      size_t in_len) {
  zx_status_t status;
  guest_ctx_t *guest;
  struct mem_region mem;
  int vmid;

  status = calculate_vmid(in_buf, &mem, in_len, &vmid);
  if (status != ZX_OK)
    return status;

  if (!grt_is_block_backend_vmid(vmid)) {
    zxlogf(ERROR, "[vblock]: vblock-drv vmid error, vmid: %d\n", vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }
  zxlogf(INFO,
         "[vblock]: calculate_vmid: %d, mem_start: 0x%lx, mem->end: 0x%lx\n",
         vmid, mem.start, mem.end);

  guest = dev->guests + vmid;

  if (GET_STATE(guest) != GUEST_STATE_UNINIT) {
    guest_ctx_exit(guest);
  }

  if (vblock_backend_needs_partition_isolation(dev)) {
    status = vpart_isolate_init((guest_ctx_t *)guest, dev, vmid);
    if (status) {
      zxlogf(ERROR, "[vblock][%d]: virt partition isolate init failed: %d\n",
             guest->vmid, status);
      return status;
    }
    zxlogf(INFO, "[vblock][%d]: virt partition isolate init succeed\n",
           guest->vmid);
  } else {
    guest->has_partition_isolation = false;
    zxlogf(INFO, "[vblock][%d]: skip partition isolate for backend %s\n",
           guest->vmid, dev->backend_name);
  }

  status = zx_fifo_create(FIFO_QDEPTH, sizeof(struct fifo_in_item), 0,
                          &guest->fifo_in[VBLOCK_FIFO_END_SELF],
                          &guest->fifo_in[VBLOCK_FIFO_END_PEER]);
  if (status) {
    zxlogf(ERROR, "[vblock][%d]: fifo_in create failed: %d\n", guest->vmid,
           status);
    goto err_fifo_in;
  }
  zxlogf(INFO, "[vblock][%d]: fifo_in create succeed\n", guest->vmid);

  status = zx_fifo_create(FIFO_QDEPTH, sizeof(struct fifo_out_item), 0,
                          &guest->fifo_out[VBLOCK_FIFO_END_SELF],
                          &guest->fifo_out[VBLOCK_FIFO_END_PEER]);
  if (status) {
    zxlogf(ERROR, "[vblock][%d]: fifo_out create failed: %d\n", guest->vmid,
           status);
    goto err_fifo_out;
  }
  zxlogf(INFO, "[vblock][%d]: fifo_out create succeed\n", guest->vmid);

  status = vblock_guest_alloc_resources(guest, &mem);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock][%d]: vblock resource alloc failed\n", guest->vmid);
    goto err_alloc;
  }

  SET_STATE(guest, GUEST_STATE_READYING);

  return ZX_OK;

err_alloc:
  zx_handle_close(guest->fifo_out[VBLOCK_FIFO_END_SELF]);
  zx_handle_close(guest->fifo_out[VBLOCK_FIFO_END_PEER]);
  guest->fifo_out[VBLOCK_FIFO_END_SELF] = ZX_HANDLE_INVALID;
  guest->fifo_out[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
err_fifo_out:
  zx_handle_close(guest->fifo_in[VBLOCK_FIFO_END_SELF]);
  zx_handle_close(guest->fifo_in[VBLOCK_FIFO_END_PEER]);
  guest->fifo_in[VBLOCK_FIFO_END_SELF] = ZX_HANDLE_INVALID;
  guest->fifo_in[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
err_fifo_in:
  vpart_isolate_release(guest);
  return status;
}

static zx_status_t vblock_guest_stop(vblock_drv_t *dev, const void *in_buf,
                                     size_t in_len) {
  if (in_len != sizeof(int32_t)) {
    zxlogf(ERROR, "[vblock]: guest stop invalid args, in_len: %lu\n", in_len);
    return ZX_ERR_INVALID_ARGS;
  }

  int32_t vmid = *(const int32_t *)in_buf;
  if (!grt_is_block_backend_vmid(vmid)) {
    zxlogf(ERROR, "[vblock]: vblock-drv stop vmid error, vmid: %d\n", vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }

  guest_ctx_t *guest = dev->guests + vmid;
  guest_ctx_exit(guest);

  mtx_lock(&dev->device_lock);
  if (dev->selected_guest_vmid == vmid) {
    dev->selected_guest_vmid = GRT_VMID_INVALID;
  }
  mtx_unlock(&dev->device_lock);

  return ZX_OK;
}

static zx_status_t vblock_set_guest_id(vblock_drv_t *dev, const void *in_buf,
                                       size_t in_len) {
  const int32_t *ptr = in_buf;
  int32_t vmid = ptr[0];

  if (!grt_is_block_backend_vmid(vmid)) {
    zxlogf(ERROR, "[vblock]: vblock-drv vmid error, vmid: %d\n", vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }

  mtx_lock(&dev->device_lock);
  if (dev->selected_guest_vmid != GRT_VMID_INVALID) {
    mtx_unlock(&dev->device_lock);
    return ZX_ERR_SHOULD_WAIT;
  }
  dev->selected_guest_vmid = vmid;
  mtx_unlock(&dev->device_lock);
  return ZX_OK;
}

static zx_status_t vblock_set_gpa_vmo(vblock_drv_t *dev, const void *in_buf,
                                      size_t in_len) {
  const zx_handle_t *ptr = in_buf;
  zx_handle_t in_gpa_vmo =
      in_len >= sizeof(*ptr) ? ptr[0] : ZX_HANDLE_INVALID;

  if (in_len != sizeof(*ptr)) {
    zxlogf(ERROR, "[vblock]: set_handle invalid args\n");
    if (in_gpa_vmo != ZX_HANDLE_INVALID) {
      zx_handle_close(in_gpa_vmo);
    }
    return ZX_ERR_INVALID_ARGS;
  }

  mtx_lock(&dev->device_lock);
  int vmid = dev->selected_guest_vmid;
  dev->selected_guest_vmid = GRT_VMID_INVALID;
  mtx_unlock(&dev->device_lock);
  if (!grt_is_block_backend_vmid(vmid)) {
    zxlogf(ERROR, "[vblock]: guest sel bad state\n");
    zx_handle_close(in_gpa_vmo);
    return ZX_ERR_BAD_STATE;
  }

  guest_ctx_t *guest = dev->guests + vmid;

  // Cleanup old vmo if set repeatedly
  if (guest->gpa_vmo != ZX_HANDLE_INVALID) {
    if (guest->mapped_vaddr) {
      uint64_t prev_sz = vmo_get_size(guest->gpa_vmo);
      if (prev_sz != UINT64_MAX) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)guest->mapped_vaddr,
                      prev_sz);
      }
      guest->mapped_vaddr = NULL;
    }
    zx_handle_close(guest->gpa_vmo);
    guest->gpa_vmo = ZX_HANDLE_INVALID;
  }

  zx_status_t status =
      zx_handle_duplicate(in_gpa_vmo, ZX_RIGHT_SAME_RIGHTS, &guest->gpa_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR,
           "[vblock][%d]: set_handle, failed to duplicate handle, ret: %d\n",
           guest->vmid, status);
    zx_handle_close(in_gpa_vmo);
    return status;
  }
  zx_handle_close(in_gpa_vmo);

  uint64_t vmo_sz, mrg_sz;
  if ((vmo_sz = vmo_get_size(guest->gpa_vmo)) !=
      (mrg_sz = mem_region_size(&guest->vm_phys_mem))) {
    zxlogf(WARN,
           "[vblock][%d]: size is not match, gpa_vmo size: 0x%lx, "
           "mem_region_size: 0x%lx\n",
           guest->vmid, vmo_sz, mrg_sz);
  }

  uintptr_t vaddr;
  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, 0,
                       guest->gpa_vmo, 0, vmo_sz, &vaddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock][%d]: set_handle, failed to map gpa_vmo, ret: %d\n",
           guest->vmid, status);
    zx_handle_close(guest->gpa_vmo);
    guest->gpa_vmo = ZX_HANDLE_INVALID;
    guest->mapped_vaddr = NULL;
    return status;
  }

  guest->mapped_vaddr = (void *)vaddr;
  atomic_thread_fence(memory_order_release);

  // Start the thread only if it has not been started yet
  if (guest->worker_thread == 0) {
    status = vblock_guest_start_thread(guest);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[vblock][%d]: failed to start worker thread, ret: %d\n",
             guest->vmid, status);
      zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)guest->mapped_vaddr, vmo_sz);
      guest->mapped_vaddr = NULL;
      zx_handle_close(guest->gpa_vmo);
      guest->gpa_vmo = ZX_HANDLE_INVALID;
      return status;
    }
    SET_STATE(guest, GUEST_STATE_READY);
  }

  return ZX_OK;
}

static zx_status_t vblock_get_handle(vblock_drv_t *dev, const void *in_buf,
                                     size_t in_len, void *out_buf,
                                     size_t out_len) {
  get_handle_param_t param = *(get_handle_param_t *)in_buf;
  if (param.vmid >= VBLOCK_MAX_GUESTS) {
    zxlogf(ERROR, "[vblock] invalid vmid: %u\n", param.vmid);
    return ZX_ERR_INVALID_ARGS;
  }
  guest_ctx_t *guest = dev->guests + param.vmid;
  if (guest && guest->vmid != param.vmid) {
    zxlogf(ERROR, "[vblock] invalid vmid: %u\n", param.vmid);
    return ZX_ERR_INVALID_ARGS;
  }

  if (GET_STATE(guest) < GUEST_STATE_READY) {
    zxlogf(
        ERROR,
        "[vblock][%u] guest VMO is not mapped yet (State: %d), IO rejected\n",
        param.vmid, GET_STATE(guest));
    return ZX_ERR_BAD_STATE;
  }

  if (out_len != sizeof(zx_handle_t)) {
    zxlogf(ERROR, "[vblock][%u] invalid output length\n", param.vmid);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  switch (param.fifo_id) {
  case 0:
    handle = guest->fifo_in[VBLOCK_FIFO_END_PEER];
    break;
  case 1:
    handle = guest->fifo_out[VBLOCK_FIFO_END_PEER];
    break;
  default:
    break;
  }
  if (handle == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "[vblock][%u] invalid fifo id\n", param.vmid);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status =
      zx_handle_replace(handle, ZX_RIGHT_SAME_RIGHTS, (zx_handle_t *)out_buf);
  if (status != ZX_OK) {
    zx_handle_close(handle);
    if (param.fifo_id == 0) {
      guest->fifo_in[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
    } else if (param.fifo_id == 1) {
      guest->fifo_out[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
    }
    return status;
  }

  if (param.fifo_id == 0) {
    guest->fifo_in[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
  } else if (param.fifo_id == 1) {
    guest->fifo_out[VBLOCK_FIFO_END_PEER] = ZX_HANDLE_INVALID;
  }

  return ZX_OK;
}

static zx_status_t vblock_get_vpi_info(vblock_drv_t *dev, const void *in_buf,
                                       size_t in_len, uint64_t *vblock_count) {
  guest_ctx_t *guest;
  int vmid = dev->selected_guest_vmid;
  if (!grt_is_block_backend_vmid(vmid)) {
    zxlogf(ERROR, "[vblock]: vblock-drv vmid error, vmid: %d\n", vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }
  guest = dev->guests + vmid;
  if (guest->has_partition_isolation) {
    vpart_isolate_get_vbinfo((guest_ctx_t *)guest, vblock_count);
  } else {
    *vblock_count = 0;
  }

  zxlogf(INFO, "[vblock]: get vpi info, vblock_count: %lu\n", *vblock_count);
  return ZX_OK;
}

static void sync_read_completion(block_op_t *block_op, zx_status_t status) {
  read_ctx_t *ctx = (read_ctx_t *)block_op->cookie;
  ctx->status = status;
  completion_signal(&ctx->completion);
}

static void vblock_direct_guest_reset(direct_io_ctx_t *direct_guest) {
  if (direct_guest->mapped_vaddr &&
      direct_guest->gpa_vmo != ZX_HANDLE_INVALID) {
    uint64_t vmar_sz = mem_region_size(&direct_guest->vm_phys_mem);
    if (vmar_sz > 0) {
      zx_vmar_unmap(zx_vmar_root_self(),
                    (uintptr_t)direct_guest->mapped_vaddr, vmar_sz);
    }
  }

  direct_guest->mapped_vaddr = NULL;

  if (direct_guest->gpa_vmo != ZX_HANDLE_INVALID) {
    zx_handle_close(direct_guest->gpa_vmo);
    direct_guest->gpa_vmo = ZX_HANDLE_INVALID;
  }

  if (direct_guest->io_vmo != ZX_HANDLE_INVALID) {
    zx_handle_close(direct_guest->io_vmo);
    direct_guest->io_vmo = ZX_HANDLE_INVALID;
  }

  memset(&direct_guest->vm_phys_mem, 0, sizeof(direct_guest->vm_phys_mem));
  direct_guest->inited = false;
}

#define MAX_XFER_SIZE (256 * 4096)
static zx_status_t sync_read_blocks(vblock_drv_t *dev,
                                    direct_io_ctx_t *direct_guest,
                                    void *buffer, uint64_t byte_offset,
                                    uint32_t byte_len) {
  zx_status_t status;
  uint32_t bsz = dev->block_info->block_size;
  block_op_t *block_op = direct_guest->io_block_op;
  zx_handle_t io_vmo = direct_guest->io_vmo;
  bool use_temp_vmo = byte_len > MAX_XFER_SIZE;
  if (!block_op) {
    zxlogf(ERROR, "[vblock]: NULL pointer in input parameters\n");
    return ZX_ERR_NO_MEMORY;
  }

  read_ctx_t ctx = {};

  if (use_temp_vmo) {
    status = zx_vmo_create(byte_len, 0, &io_vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[vblock]: Failed to create temp vmo, size: %u, ret: %d\n",
             byte_len, status);
      return status;
    }
  } else if (io_vmo == ZX_HANDLE_INVALID) {
    status = zx_vmo_create(MAX_XFER_SIZE, 0, &io_vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[vblock]: Failed to create vmo\n");
      return status;
    }
    direct_guest->io_vmo = io_vmo;
  }

  block_op->command = BLOCK_OP_READ;
  block_op->rw.vmo = io_vmo;
  block_op->rw.length = byte_len / bsz;
  block_op->rw.offset_dev = byte_offset / bsz;
  block_op->rw.offset_vmo = 0;
  block_op->completion_cb = sync_read_completion;
  block_op->cookie = &ctx;

  completion_reset(&ctx.completion);
  dev->block_proto->ops->queue(dev->block_proto->ctx, block_op);
  completion_wait(&ctx.completion, ZX_TIME_INFINITE);

  if (ctx.status != ZX_OK) {
    zxlogf(ERROR, "[vblock]: Block I/O callback failed, ret: %d\n", ctx.status);
    if (use_temp_vmo) {
      zx_handle_close(io_vmo);
    }
    return ctx.status;
  }

  status = zx_vmo_read(io_vmo, buffer, 0, byte_len);
  if (use_temp_vmo) {
    zx_handle_close(io_vmo);
  }
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock]: zx_vmo_read failed, bsz: %u, status: %d\n", bsz,
           status);
    return ZX_ERR_INTERNAL;
  }

  return ctx.status;
}

static zx_status_t vblock_direct_guest_start(vblock_drv_t *dev,
                                             const void *in_buf,
                                             size_t in_len) {
  zxlogf(INFO, "[vblock]: direct vblock ioctl start\n");
  struct mem_region mem;
  int vmid;

  zx_status_t status = calculate_vmid(in_buf, &mem, in_len, &vmid);
  if (status != ZX_OK) {
    return status;
  }
  if (vmid >= VBLOCK_MAX_GUESTS || vmid < 0) {
    zxlogf(ERROR, "[vblock]: direct vblock vmid error, vmid: %d\n", vmid);
    return ZX_ERR_INVALID_ARGS;
  }

  direct_io_ctx_t *direct_guest = &dev->direct_guest[vmid];
  if (direct_guest->inited || direct_guest->gpa_vmo != ZX_HANDLE_INVALID ||
      direct_guest->mapped_vaddr) {
    vblock_direct_guest_reset(direct_guest);
  }

  direct_guest->vm_phys_mem = mem;

  if (!direct_guest->io_block_op) {
    direct_guest->io_block_op = calloc(1, dev->block_op_size);
    if (!direct_guest->io_block_op) {
      zxlogf(ERROR, "[vblock]: NULL pointer in input parameters\n");
      return ZX_ERR_NO_MEMORY;
    }
  }

  if (direct_guest->io_vmo == ZX_HANDLE_INVALID) {
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    status = zx_vmo_create(MAX_XFER_SIZE, 0, &vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[vblock]: Failed to create vmo\n");
      return status;
    }
    direct_guest->io_vmo = vmo;
  }

  mtx_lock(&dev->device_lock);
  dev->selected_direct_vmid = vmid;
  mtx_unlock(&dev->device_lock);

  return ZX_OK;
}

static zx_status_t vblock_direct_set_gpa_range(vblock_drv_t *dev,
                                               const void *in_buf,
                                               size_t in_len) {
  const zx_handle_t *ptr = in_buf;
  zx_handle_t in_gpa_vmo =
      in_len >= sizeof(*ptr) ? ptr[0] : ZX_HANDLE_INVALID;
  if (sizeof(*ptr) != in_len) {
    zxlogf(
        ERROR,
        "[vblock]: set gpa range invalid args, struct len: %lu, in_len: %lu\n",
        sizeof(*ptr), in_len);
    if (in_gpa_vmo != ZX_HANDLE_INVALID) {
      zx_handle_close(in_gpa_vmo);
    }
    return ZX_ERR_INVALID_ARGS;
  }

  mtx_lock(&dev->device_lock);
  int vmid = dev->selected_direct_vmid;
  dev->selected_direct_vmid = GRT_VMID_INVALID;
  mtx_unlock(&dev->device_lock);
  if (vmid >= VBLOCK_MAX_GUESTS || vmid < 0) {
    zxlogf(ERROR, "[vblock]: direct set gpa vmid error, vmid: %d\n", vmid);
    zx_handle_close(in_gpa_vmo);
    return ZX_ERR_BAD_STATE;
  }

  direct_io_ctx_t *direct_guest = &dev->direct_guest[vmid];
  if (direct_guest->gpa_vmo != ZX_HANDLE_INVALID) {
    if (direct_guest->mapped_vaddr) {
      uint64_t prev_sz = mem_region_size(&direct_guest->vm_phys_mem);
      if (prev_sz > 0) {
        zx_vmar_unmap(zx_vmar_root_self(),
                      (uintptr_t)direct_guest->mapped_vaddr, prev_sz);
      }
      direct_guest->mapped_vaddr = NULL;
    }
    zx_handle_close(direct_guest->gpa_vmo);
    direct_guest->gpa_vmo = ZX_HANDLE_INVALID;
  }

  zx_status_t status = zx_handle_duplicate(
      in_gpa_vmo, ZX_RIGHT_SAME_RIGHTS, &direct_guest->gpa_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock]: failed to duplicate handle, ret: %d\n", status);
    zx_handle_close(in_gpa_vmo);
    return status;
  }
  zx_handle_close(in_gpa_vmo);

  uint64_t vmo_sz, mrg_sz;
  if ((vmo_sz = vmo_get_size(direct_guest->gpa_vmo)) !=
      (mrg_sz = mem_region_size(&direct_guest->vm_phys_mem))) {
    zxlogf(ERROR,
           "[vblock]: size is not match, gpa_vmo size: 0x%lx, mem_region_size: "
           "0x%lx\n",
           vmo_sz, mrg_sz);
    zx_handle_close(direct_guest->gpa_vmo);
    direct_guest->gpa_vmo = ZX_HANDLE_INVALID;
    direct_guest->mapped_vaddr = NULL;
    return ZX_ERR_INVALID_ARGS;
  }

  uintptr_t vaddr;
  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, 0,
                       direct_guest->gpa_vmo, 0, vmo_sz, &vaddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock]: failed to map gpa_vmo, ret: %d\n", status);
    zx_handle_close(direct_guest->gpa_vmo);
    direct_guest->gpa_vmo = ZX_HANDLE_INVALID;
    direct_guest->mapped_vaddr = NULL;
    return status;
  }

  direct_guest->mapped_vaddr = (void *)vaddr;
  direct_guest->inited = true;

  return ZX_OK;
}

static zx_status_t vblock_direct_read(vblock_drv_t *dev, const void *in_buf,
                                      size_t in_len) {
  if (in_len != sizeof(struct direct_read_param)) {
    zxlogf(ERROR, "[vblock]: direct read invalid args, in_len: %lu\n", in_len);
    return ZX_ERR_INVALID_ARGS;
  }

  const struct direct_read_param *param = in_buf;
  int32_t vmid = param->vmid;
  struct direct_io_rw_param rw = param->rw;

  if (vmid >= VBLOCK_MAX_GUESTS || vmid < 0) {
    zxlogf(ERROR, "[vblock]: direct read vmid error, vmid: %d\n", vmid);
    return ZX_ERR_INVALID_ARGS;
  }

  direct_io_ctx_t *direct_guest = &dev->direct_guest[vmid];
  if (!direct_guest->inited || !direct_guest->mapped_vaddr) {
    zxlogf(ERROR, "[vblock]: direct read before guest memory is ready\n");
    return ZX_ERR_BAD_STATE;
  }

  if (rw.addr < direct_guest->vm_phys_mem.start ||
      rw.addr > direct_guest->vm_phys_mem.end ||
      rw.len > direct_guest->vm_phys_mem.end - rw.addr + 1) {
    zxlogf(ERROR,
           "[vblock]: direct read addr out of range, vmid: %d, addr: 0x%lx, "
           "len: %u, mem: 0x%lx-0x%lx\n",
           vmid, rw.addr, rw.len, direct_guest->vm_phys_mem.start,
           direct_guest->vm_phys_mem.end);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t offset = rw.addr - direct_guest->vm_phys_mem.start;
  void *read_buf = (void *)((uint8_t *)direct_guest->mapped_vaddr + offset);

  zx_status_t status =
      sync_read_blocks(dev, direct_guest, read_buf, rw.offset, rw.len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[vblock]: read failed, error code: %d\n", status);
  }
  return status;
}

zx_status_t vblock_dev_ioctl(vblock_drv_t *dev, uint32_t op, const void *in_buf,
                             size_t in_len, void *out_buf, size_t out_len,
                             size_t *out_actual) {
  zx_status_t status;

  *out_actual = 0;

  switch (op) {
  case IOCTL_VBLOCK_START:
    return vblock_guest_start(dev, in_buf, in_len);
  case IOCTL_VBLOCK_STOP:
    return vblock_guest_stop(dev, in_buf, in_len);
  case IOCTL_VBLOCK_GET_FIFO_HANDLES:
    status = vblock_get_handle(dev, in_buf, in_len, out_buf, out_len);
    if (status == ZX_OK)
      *out_actual = out_len;
    return status;
  case IOCTL_VBLOCK_SET_GUEST:
    return vblock_set_guest_id(dev, in_buf, in_len);
  case IOCTL_VBLOCK_GET_INFO:
    status = vblock_get_vpi_info(dev, in_buf, in_len, out_buf);
    if (status == ZX_OK)
      *out_actual = sizeof(uint64_t);
    return status;
  case IOCTL_VBLOCK_SET_GPA_RANGE:
    return vblock_set_gpa_vmo(dev, in_buf, in_len);
  case IOCTL_VBLOCK_DIRECT_START:
    return vblock_direct_guest_start(dev, in_buf, in_len);
  case IOCTL_VBLOCK_DIRECT_SET_GPA_RANGE:
    return vblock_direct_set_gpa_range(dev, in_buf, in_len);
  case IOCTL_VBLOCK_DIRECT_READ:
    return vblock_direct_read(dev, in_buf, in_len);
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void vblock_dev_release(vblock_drv_t *dev) {
  guest_ctx_t *guest;
  for (int i = 0; i < VBLOCK_MAX_GUESTS; i++) {
    guest = dev->guests + i;
    guest_ctx_exit(guest);

    vblock_direct_guest_reset(&dev->direct_guest[i]);
    if (dev->direct_guest[i].io_block_op) {
      free(dev->direct_guest[i].io_block_op);
      dev->direct_guest[i].io_block_op = NULL;
    }
  }

  free(dev);
}

void vblock_dev_init(vblock_drv_t *dev) {
  int i = 0;

  for (; i < VBLOCK_MAX_GUESTS; ++i) {
    guest_ctx_t *guest = dev->guests + i;
    guest_ctx_init(guest);
    guest->vmid = get_guest_vmid(dev, guest);

    dev->direct_guest[i].gpa_vmo = ZX_HANDLE_INVALID;
    dev->direct_guest[i].io_vmo = ZX_HANDLE_INVALID;
    dev->direct_guest[i].io_block_op = NULL;
    dev->direct_guest[i].inited = false;
  }

  dev->selected_guest_vmid = GRT_VMID_INVALID;
  dev->selected_direct_vmid = GRT_VMID_INVALID;
  mtx_init(&dev->device_lock, mtx_plain);
}

zx_status_t vblock_dev_set_backend(vblock_drv_t *dev, block_protocol_t *bp,
                                   block_info_t *info, size_t block_op_size,
                                   const char *backend_name) {
  dev->block_proto = bp;
  dev->block_info = info;
  dev->block_op_size = ALIGN(block_op_size, sizeof(void *));
  if (backend_name) {
    snprintf(dev->backend_name, sizeof(dev->backend_name), "%s", backend_name);
  } else {
    dev->backend_name[0] = '\0';
  }
  zxlogf(INFO,
         "[vblock]: dev->block_op_size: %lu, sizeof(block_op_t): %lu, "
         "info->block_count: %lu, backend: %s\n",
         dev->block_op_size, sizeof(block_op_t), info->block_count,
         dev->backend_name);

  return ZX_OK;
}
