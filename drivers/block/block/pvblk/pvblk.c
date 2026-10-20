// SPDX-License-Identifier: BSD-3-Clause

#include "pvblk.h"

#include <ddk/debug.h>
#include <zircon/device/block.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../blk_crypto.h"
#include "../vblock-drv/vpart_isolate.h"
#include "blk_retry/retry.h"
#include "garnet/lib/vm_id/vm_id.h"

#define PVBLK_SECTOR_SIZE 512U
#define PVBLK_SECTOR_SHIFT 9U
#define PVBLK_RELEASE_WAIT_MSEC 100
#define PVBLK_SUPPORTED_ERASE_SEGS 1U
#define PVBLK_MAX_32BIT_SECTORS UINT32_MAX
#define PVBLK_NATIVE_SG_RW_MAX_BYTES (256U * 1024U)
#define PVBLK_LEGACY_SIGNAL_READ_MIN_BYTES (256U * 1024U)
#define PVBLK_EAGER_SIGNAL_READ_MIN_BYTES (256U * 1024U)
/*
 * High-QD libaio benefits from suppressing redundant backend events, but opt51
 * showed that strict CQ empty-edge signaling can delay useful progress. Keep
 * shallow backlogs on the legacy irq-arm path and send bounded progress events
 * only while CQ remains non-empty under sustained completion pressure.
 */
#define PVBLK_COMPLETION_SIGNAL_SMALL_BACKLOG 4U
#define PVBLK_COMPLETION_SIGNAL_BATCH 16U
#define PVBLK_COMPLETION_SIGNAL_BACKLOG 32U
#define PVBLK_COMPLETION_SIGNAL_EAGER_READ_SMALL_BACKLOG 8U
#define PVBLK_COMPLETION_SIGNAL_EAGER_READ_BATCH 4U
#define PVBLK_COMPLETION_SIGNAL_EAGER_READ_BACKLOG 16U
/*
 * Keep the host-side assist shorter than the guest pre-arm poll window. This
 * wait runs inside the synchronous MMIO doorbell path; stretching it to match
 * the guest poll budget blocks the guest from polling its own CQ and regresses
 * read/unbound psync latency when UFS completion is slower than the assist.
 */
#define PVBLK_COMPLETION_ASSIST_USEC 40U

static uint64_t pvblk_mem_region_size(const struct pvblk_mem_region* mem) {
  return mem->end - mem->start + 1U;
}

static uint64_t pvblk_vmo_get_size(zx_handle_t vmo) {
  uint64_t size;
  if (zx_vmo_get_size(vmo, &size) == ZX_OK) {
    return size;
  }
  return UINT64_MAX;
}

static bool pvblk_sector_to_bytes(uint64_t sector, uint64_t* out_bytes) {
  if (!out_bytes || sector > (UINT64_MAX >> PVBLK_SECTOR_SHIFT)) {
    return false;
  }
  *out_bytes = sector << PVBLK_SECTOR_SHIFT;
  return true;
}

static uint32_t pvblk_bytes_to_sectors_limit(uint64_t bytes) {
  uint64_t sectors = bytes >> PVBLK_SECTOR_SHIFT;

  if (sectors > PVBLK_MAX_32BIT_SECTORS) {
    return PVBLK_MAX_32BIT_SECTORS;
  }
  return (uint32_t)sectors;
}

#define PVBLK_SQE_HOT_SIZE offsetof(struct pvblk_sqe, crypto_dun)

static void pvblk_sqe_copy_for_slot(struct pvblk_sqe* dst,
                                    const struct pvblk_sqe* src) {
  if (!dst || !src) {
    return;
  }

  if (src->flags & PVBLK_SQE_F_USE_CRYPTO) {
    *dst = *src;
    return;
  }

  memcpy(dst, src, PVBLK_SQE_HOT_SIZE);
  uint16_t nr_segments = src->nr_segments;
  if (nr_segments > PVBLK_MAX_INLINE_SEGS) {
    nr_segments = PVBLK_MAX_INLINE_SEGS;
  }
  if (nr_segments) {
    memcpy(dst->segs, src->segs, nr_segments * sizeof(dst->segs[0]));
  }
}

static void pvblk_sqe_clear_for_recycle(struct pvblk_sqe* sqe) {
  if (!sqe) {
    return;
  }
  uint16_t flags = sqe->flags;
  uint16_t nr_segments = sqe->nr_segments;
  if (flags & PVBLK_SQE_F_USE_CRYPTO) {
    memset(sqe, 0, sizeof(*sqe));
    return;
  }
  memset(sqe, 0, PVBLK_SQE_HOT_SIZE);
  if (nr_segments > PVBLK_MAX_INLINE_SEGS) {
    nr_segments = PVBLK_MAX_INLINE_SEGS;
  }
  if (nr_segments) {
    memset(sqe->segs, 0, nr_segments * sizeof(sqe->segs[0]));
  }
}

static bool pvblk_feature_supported(const pvblk_dev_t* dev, uint16_t op) {
  const block_info_t* info = dev ? dev->block_info : NULL;

  if (!info) {
    return false;
  }
  switch (op) {
    case PVBLK_OP_READ:
    case PVBLK_OP_WRITE:
      return true;
    case PVBLK_OP_FLUSH:
      return true;
    case PVBLK_OP_DISCARD:
      return info->discard_max_bytes != 0 && info->discard_max_segments != 0;
    case PVBLK_OP_WRITE_ZEROES:
      return info->write_zeroes_max_bytes != 0 &&
             info->write_zeroes_max_segments != 0;
    case PVBLK_OP_SECURE_ERASE:
      return info->secure_erase_max_bytes != 0 &&
             info->secure_erase_max_segments != 0;
    default:
      return false;
  }
}

static uint16_t pvblk_supported_max_segs(const block_info_t* info) {
  if (!info || info->max_seg_nums == 0) {
    return 1U;
  }
  return info->max_seg_nums < PVBLK_MAX_INLINE_SEGS ? info->max_seg_nums
                                                    : PVBLK_MAX_INLINE_SEGS;
}

static void pvblk_crypto_cap_to_wire(const struct blk_crypto_cap* in,
                                     struct pvblk_crypto_cap* out) {
  if (!in || !out) {
    return;
  }
  out->max_dun_bytes_supported = in->max_dun_bytes_supported;
  out->key_types_supported = in->key_types_supported;
  for (uint32_t i = 0; i < PVBLK_CRYPTO_MODE_COUNT; ++i) {
    out->modes_supported[i] = i < BLK_ENCRYPTION_MODE_MAX
                                  ? in->modes_supported[i]
                                  : 0U;
  }
}

static void pvblk_crypto_key_from_wire(const struct pvblk_crypto_key* in,
                                       struct blk_crypto_key* out) {
  if (!in || !out) {
    return;
  }

  memset(out, 0, sizeof(*out));
  out->crypto_cfg.crypto_mode =
      (enum blk_crypto_mode_num)in->crypto_cfg.crypto_mode;
  out->crypto_cfg.data_unit_size = in->crypto_cfg.data_unit_size;
  out->crypto_cfg.dun_bytes = in->crypto_cfg.dun_bytes;
  out->crypto_cfg.key_type =
      (enum blk_crypto_key_type)in->crypto_cfg.key_type;
  out->data_unit_size_bits = in->data_unit_size_bits;
  out->size = in->size <= sizeof(out->raw) ? in->size : sizeof(out->raw);
  memcpy(out->raw, in->raw, out->size);
}

static bool pvblk_crypto_dun_add(
    const uint64_t in[PVBLK_CRYPTO_DUN_ARRAY_SIZE],
    uint64_t increment,
    uint64_t out[PVBLK_CRYPTO_DUN_ARRAY_SIZE]) {
  memcpy(out, in, sizeof(uint64_t) * PVBLK_CRYPTO_DUN_ARRAY_SIZE);
  if (!increment) {
    return true;
  }

  uint64_t old = out[0];
  out[0] += increment;
  bool carry = out[0] < old;
  for (uint32_t i = 1; carry && i < PVBLK_CRYPTO_DUN_ARRAY_SIZE; ++i) {
    old = out[i];
    out[i]++;
    carry = out[i] < old;
  }
  return !carry;
}

static int pvblk_setup_crypto(pvblk_dev_t* dev,
                              pvblk_guest_ctx_t* ctx,
                              pvblk_slot_t* slot,
                              block_op_t* block_op,
                              uint32_t segment_index,
                              uint64_t crypto_byte_offset) {
  struct pvblk_sqe* sqe = slot ? &slot->sqe : NULL;
  struct blk_crypto_profile* profile =
      (dev && dev->block_proto && dev->block_proto->ops)
          ? dev->block_proto->ops->crypto_profile
          : NULL;
  struct blk_crypto_key* key;
  uint64_t slot_mask;
  uint64_t toggle_mask;
  uint64_t current_toggle;
  uint64_t requested_toggle;
  int slot_index;
  int ret;

  if (!sqe || !(sqe->flags & PVBLK_SQE_F_USE_CRYPTO)) {
    block_op_crypto_ctx(block_op) = NULL;
    return 0;
  }
  if (!dev->block_info || !dev->block_info->inline_crypto_supported ||
      !profile || !ctx || segment_index >= PVBLK_MAX_INLINE_SEGS ||
      sqe->crypto_key_index >= PVBLK_CRYPTO_KEY_SLOT_NUM) {
    return -1;
  }

  slot_index = sqe->crypto_key_index;
  key = &ctx->crypto_keys[slot_index];
  slot_mask = 1ULL << slot_index;
  toggle_mask = 1ULL << (32U + slot_index);
  current_toggle = ctx->crypto_key_bitmap & toggle_mask;
  requested_toggle =
      (sqe->crypto_bmap & 0x1U) ? toggle_mask : 0U;

  if (current_toggle != requested_toggle) {
    if (ctx->crypto_key_bitmap & slot_mask) {
      __blk_crypto_evict_key(profile, key);
    }
    ctx->crypto_key_bitmap &= ~(slot_mask | toggle_mask);
    pvblk_crypto_key_from_wire(&sqe->crypto_key, key);
    blk_crypto_key_set_priv(key, (uint8_t)slot_index);
    if (!__blk_crypto_cfg_supported(profile, &key->crypto_cfg)) {
      return -1;
    }
    ctx->crypto_key_bitmap |= slot_mask;
    ctx->crypto_key_bitmap =
        (ctx->crypto_key_bitmap & ~toggle_mask) | requested_toggle;
  } else if (!(ctx->crypto_key_bitmap & slot_mask)) {
    pvblk_crypto_key_from_wire(&sqe->crypto_key, key);
    blk_crypto_key_set_priv(key, (uint8_t)slot_index);
    if (!__blk_crypto_cfg_supported(profile, &key->crypto_cfg)) {
      return -1;
    }
    ctx->crypto_key_bitmap |= slot_mask;
  }

  block_op_crypto_ctx(block_op) = &slot->crypto_ctx[segment_index];
  memset(block_op_crypto_ctx(block_op), 0,
         sizeof(slot->crypto_ctx[segment_index]));
  if (key->crypto_cfg.data_unit_size == 0 ||
      crypto_byte_offset % key->crypto_cfg.data_unit_size) {
    block_op_crypto_ctx(block_op) = NULL;
    return -1;
  }
  if (!pvblk_crypto_dun_add(sqe->crypto_dun,
                            crypto_byte_offset / key->crypto_cfg.data_unit_size,
                            block_op_crypto_dun(block_op))) {
    block_op_crypto_ctx(block_op) = NULL;
    return -1;
  }
  block_op_crypto_key(block_op) = key;
  ret = blk_crypto_get_keyslot(profile, key, &block_op_crypto_keyslot(block_op));
  return ret == 0 ? 0 : -1;
}

static uint64_t pvblk_erase_max_bytes(const block_info_t* info, uint16_t op) {
  if (!info) {
    return 0;
  }
  switch (op) {
    case PVBLK_OP_DISCARD:
      return info->discard_max_bytes;
    case PVBLK_OP_WRITE_ZEROES:
      return info->write_zeroes_max_bytes;
    case PVBLK_OP_SECURE_ERASE:
      return info->secure_erase_max_bytes;
    default:
      return 0;
  }
}

static uint32_t pvblk_erase_alignment(const block_info_t* info, uint16_t op) {
  if (!info) {
    return 0;
  }
  switch (op) {
    case PVBLK_OP_DISCARD:
      return info->discard_granularity ? info->discard_granularity
                                       : info->block_size;
    case PVBLK_OP_SECURE_ERASE:
      return info->secure_erase_granularity ? info->secure_erase_granularity
                                            : info->block_size;
    case PVBLK_OP_WRITE_ZEROES:
      return info->block_size;
    default:
      return 0;
  }
}

static bool pvblk_erase_flags_valid(const struct pvblk_sqe* sqe) {
  if (!sqe) {
    return false;
  }
  switch (sqe->op) {
    case PVBLK_OP_DISCARD:
    case PVBLK_OP_SECURE_ERASE:
      return sqe->flags == 0;
    case PVBLK_OP_WRITE_ZEROES:
      return (sqe->flags & ~PVBLK_SQE_F_WRITE_ZEROES_UNMAP) == 0;
    default:
      return false;
  }
}

static void pvblk_counter_inc(uint64_t* counter) {
  __atomic_fetch_add(counter, 1, __ATOMIC_RELAXED);
}

static void pvblk_counter_add(uint64_t* counter, uint64_t value) {
  __atomic_fetch_add(counter, value, __ATOMIC_RELAXED);
}

static uint64_t pvblk_counter_load(const uint64_t* counter) {
  return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

static uint32_t pvblk_ctx_state_load(const pvblk_guest_ctx_t* ctx) {
  return ctx ? __atomic_load_n(&ctx->state, __ATOMIC_ACQUIRE)
             : PVBLK_GUEST_UNINIT;
}

static void pvblk_ctx_state_store(pvblk_guest_ctx_t* ctx, uint32_t state) {
  __atomic_store_n(&ctx->state, state, __ATOMIC_RELEASE);
}

/*
 * A STOP must prevent a doorbell from entering after it starts, while allowing
 * a doorbell already admitted to finish recording every submitted request.
 */
static bool pvblk_guest_ctx_begin_doorbell(pvblk_guest_ctx_t* ctx) {
  if (!ctx) {
    return false;
  }

  mtx_lock(&ctx->lifecycle_lock);
  bool accepted = pvblk_ctx_state_load(ctx) == PVBLK_GUEST_RING_READY;
  if (accepted) {
    ctx->active_doorbells++;
  }
  mtx_unlock(&ctx->lifecycle_lock);
  return accepted;
}

static void pvblk_guest_ctx_end_doorbell(pvblk_guest_ctx_t* ctx) {
  if (!ctx) {
    return;
  }

  mtx_lock(&ctx->lifecycle_lock);
  if (ctx->active_doorbells == 0) {
    mtx_unlock(&ctx->lifecycle_lock);
    return;
  }
  ctx->active_doorbells--;
  mtx_unlock(&ctx->lifecycle_lock);
}

static void pvblk_counter_clear(uint64_t* counter) {
  __atomic_store_n(counter, 0, __ATOMIC_RELAXED);
}

static void pvblk_clear_counter_array(uint64_t* counters, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    pvblk_counter_clear(&counters[i]);
  }
}

static void pvblk_guest_stats_reset(pvblk_guest_ctx_t* ctx) {
  if (!ctx) {
    return;
  }
  pvblk_clear_counter_array(ctx->read_sqe_segments,
                            PVBLK_MAX_INLINE_SEGS + 1U);
  pvblk_clear_counter_array(ctx->write_sqe_segments,
                            PVBLK_MAX_INLINE_SEGS + 1U);
  pvblk_counter_clear(&ctx->read_sqes);
  pvblk_counter_clear(&ctx->write_sqes);
  pvblk_counter_clear(&ctx->read_block_ops);
  pvblk_counter_clear(&ctx->write_block_ops);
  pvblk_counter_clear(&ctx->read_multi_op_sqes);
  pvblk_counter_clear(&ctx->write_multi_op_sqes);
  pvblk_counter_clear(&ctx->read_native_sg_sqes);
  pvblk_counter_clear(&ctx->read_native_sg_segments);
  pvblk_counter_clear(&ctx->write_native_sg_sqes);
  pvblk_counter_clear(&ctx->write_native_sg_segments);
  pvblk_counter_clear(&ctx->multi_op_completion_waits);
  pvblk_counter_clear(&ctx->multi_op_completion_done);
  pvblk_counter_clear(&ctx->guest_signal_count);
  pvblk_counter_clear(&ctx->guest_signal_suppressed);
}

static void pvblk_guest_record_submit_stats(pvblk_guest_ctx_t* ctx,
                                            const pvblk_slot_t* slot,
                                            uint32_t op_count) {
  if (!ctx || !slot || op_count == 0) {
    return;
  }
  uint32_t segs = slot->sqe.nr_segments;
  if (segs > PVBLK_MAX_INLINE_SEGS) {
    segs = PVBLK_MAX_INLINE_SEGS;
  }
  switch (slot->sqe.op) {
    case PVBLK_OP_READ:
      pvblk_counter_inc(&ctx->read_sqes);
      pvblk_counter_add(&ctx->read_block_ops, op_count);
      if (segs > 0) {
        pvblk_counter_inc(&ctx->read_sqe_segments[segs]);
      }
      if (op_count > 1U) {
        pvblk_counter_inc(&ctx->read_multi_op_sqes);
      }
      if (op_count == 1U && slot->block_op &&
          slot->block_op->rw.sg_count > 1U) {
        pvblk_counter_inc(&ctx->read_native_sg_sqes);
        pvblk_counter_add(&ctx->read_native_sg_segments,
                          slot->block_op->rw.sg_count);
      }
      break;
    case PVBLK_OP_WRITE:
      pvblk_counter_inc(&ctx->write_sqes);
      pvblk_counter_add(&ctx->write_block_ops, op_count);
      if (segs > 0) {
        pvblk_counter_inc(&ctx->write_sqe_segments[segs]);
      }
      if (op_count > 1U) {
        pvblk_counter_inc(&ctx->write_multi_op_sqes);
      }
      if (op_count == 1U && slot->block_op &&
          slot->block_op->rw.sg_count > 1U) {
        pvblk_counter_inc(&ctx->write_native_sg_sqes);
        pvblk_counter_add(&ctx->write_native_sg_segments,
                          slot->block_op->rw.sg_count);
      }
      break;
    default:
      break;
  }
}

static bool pvblk_backend_needs_partition_isolation(pvblk_dev_t* dev) {
  return dev && strncmp(dev->backend_name, "sdc", strlen("sdc")) == 0;
}

static uint64_t pvblk_effective_block_count(pvblk_dev_t* dev,
                                            pvblk_guest_ctx_t* ctx) {
  if (!dev || !dev->block_info) {
    return 0;
  }
  if (ctx && ctx->has_partition_isolation && ctx->vpi_drv) {
    uint64_t block_count = vpart_isolate_get_block_count(ctx->vpi_drv);
    if (block_count != 0) {
      return block_count;
    }
  }
  return dev->block_info->block_count;
}

static bool pvblk_guest_valid(const pvblk_guest_t* guest) {
  return guest && guest->queues && guest->queue_count > 0 &&
         guest->queue_count <= PVBLK_MAX_QUEUES &&
         pvblk_queue_depth_valid(guest->queue_depth);
}

static pvblk_queue_t* pvblk_get_queue(pvblk_guest_t* guest, uint16_t qid) {
  if (!pvblk_guest_valid(guest) || qid >= guest->queue_count) {
    return NULL;
  }
  return &guest->queues[qid];
}

static pvblk_guest_ctx_t* pvblk_slot_ctx(pvblk_slot_t* slot) {
  return slot ? slot->ctx : NULL;
}

static struct pvblk_queue_ctrl* pvblk_queue_ctrl(pvblk_queue_t* queue) {
  return queue->ring.ctrl ? queue->ring.ctrl : &queue->ctrl;
}

static bool pvblk_ring_valid(const pvblk_ring_t* ring, uint32_t queue_depth) {
  return ring && ring->ctrl && ring->sq && ring->cq &&
         ring->depth == queue_depth && pvblk_queue_depth_valid(ring->depth);
}

static bool pvblk_ctx_started(const pvblk_guest_ctx_t* ctx) {
  uint32_t state = pvblk_ctx_state_load(ctx);
  return ctx && state >= PVBLK_GUEST_STARTED &&
         state < PVBLK_GUEST_STOPPING && pvblk_guest_valid(&ctx->guest);
}

static pvblk_guest_ctx_t* pvblk_dev_get_guest(pvblk_dev_t* dev, uint16_t vmid) {
  if (!dev || !grt_is_block_backend_vmid((int32_t)vmid)) {
    return NULL;
  }
  return &dev->guests[vmid];
}

static void* pvblk_guest_gpa_to_vaddr(pvblk_guest_ctx_t* ctx,
                                      uint64_t gpa,
                                      uint64_t len) {
  if (!ctx || !ctx->mapped_vaddr || len == 0 ||
      ctx->vm_phys_mem.end < ctx->vm_phys_mem.start) {
    return NULL;
  }

  uint64_t region_size = pvblk_mem_region_size(&ctx->vm_phys_mem);
  uint64_t offset;
  if (gpa >= ctx->vm_phys_mem.start) {
    offset = gpa - ctx->vm_phys_mem.start;
  } else {
    offset = gpa;
  }
  if (offset >= region_size || len > region_size - offset ||
      offset >= ctx->mapped_size || len > ctx->mapped_size - offset) {
    return NULL;
  }
  return (uint8_t*)ctx->mapped_vaddr + offset;
}

static void pvblk_guest_ctx_reset(pvblk_guest_ctx_t* ctx) {
  if (!ctx) {
    return;
  }
  uint32_t vmid = ctx->guest.vmid;
  memset((uint8_t*)ctx + offsetof(pvblk_guest_ctx_t, active_doorbells), 0,
         sizeof(*ctx) - offsetof(pvblk_guest_ctx_t, active_doorbells));
  ctx->guest.vmid = vmid;
  ctx->gpa_vmo = ZX_HANDLE_INVALID;
  ctx->completion_event = ZX_HANDLE_INVALID;
  pvblk_ctx_state_store(ctx, PVBLK_GUEST_UNINIT);
  ctx->vpi_drv = NULL;
  ctx->has_partition_isolation = false;
}

static void pvblk_guest_evict_crypto_keys(pvblk_guest_ctx_t* ctx) {
  if (!ctx || !ctx->crypto_profile) {
    return;
  }
  for (uint32_t i = 0; i < PVBLK_CRYPTO_KEY_SLOT_NUM; ++i) {
    uint64_t mask = 1ULL << i;
    if (!(ctx->crypto_key_bitmap & mask)) {
      continue;
    }
    __blk_crypto_evict_key(ctx->crypto_profile, &ctx->crypto_keys[i]);
  }
  ctx->crypto_key_bitmap = 0;
  ctx->crypto_profile = NULL;
}

static void pvblk_guest_ctx_release(pvblk_guest_ctx_t* ctx) {
  if (!ctx) {
    return;
  }

  mtx_lock(&ctx->lifecycle_lock);
  if (pvblk_ctx_state_load(ctx) != PVBLK_GUEST_UNINIT) {
    pvblk_ctx_state_store(ctx, PVBLK_GUEST_STOPPING);
  }

  while (ctx->active_doorbells > 0 ||
         pvblk_counter_load(&ctx->request_count) !=
             pvblk_counter_load(&ctx->completed_count)) {
    mtx_unlock(&ctx->lifecycle_lock);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(PVBLK_RELEASE_WAIT_MSEC)));
    mtx_lock(&ctx->lifecycle_lock);
  }
  mtx_unlock(&ctx->lifecycle_lock);

  if (ctx->mapped_vaddr && ctx->mapped_size > 0) {
    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)ctx->mapped_vaddr,
                  ctx->mapped_size);
  }
  ctx->mapped_vaddr = NULL;
  ctx->mapped_size = 0;

  if (ctx->gpa_vmo != ZX_HANDLE_INVALID) {
    zx_handle_close(ctx->gpa_vmo);
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
  }
  if (ctx->completion_event != ZX_HANDLE_INVALID) {
    zx_handle_close(ctx->completion_event);
    ctx->completion_event = ZX_HANDLE_INVALID;
  }
  pvblk_guest_evict_crypto_keys(ctx);
  vpart_isolate_destroy(ctx->vpi_drv);
  ctx->vpi_drv = NULL;
  ctx->has_partition_isolation = false;

  pvblk_guest_detach_rings(&ctx->guest);
  pvblk_guest_release(&ctx->guest);
  free(ctx->requests_buffer);
  ctx->requests_buffer = NULL;
  ctx->request_stride = 0;
  ctx->slot_count = 0;
  pvblk_guest_ctx_reset(ctx);
}

zx_status_t pvblk_guest_init(pvblk_guest_t* guest,
                             uint32_t vmid,
                             pvblk_queue_t* queues,
                             uint32_t queue_count,
                             pvblk_slot_t* slots,
                             uint32_t slot_count,
                             uint32_t queue_depth) {
  if (!guest || !queues || !slots || queue_count == 0 ||
      queue_count > PVBLK_MAX_QUEUES || !pvblk_queue_depth_valid(queue_depth) ||
      slot_count < queue_count * queue_depth) {
    return ZX_ERR_INVALID_ARGS;
  }

  memset(guest, 0, sizeof(*guest));
  guest->vmid = vmid;
  guest->queue_count = queue_count;
  guest->queue_depth = queue_depth;
  guest->queues = queues;

  memset(queues, 0, sizeof(*queues) * queue_count);
  memset(slots, 0, sizeof(*slots) * slot_count);

  for (uint32_t q = 0; q < queue_count; ++q) {
    pvblk_queue_t* queue = &queues[q];
    queue->qid = (uint16_t)q;
    queue->depth = queue_depth;
    queue->mask = pvblk_queue_mask(queue_depth);
    queue->irq_arm_mode = 0;
    queue->last_irq_seq = 0;
    queue->completion_signal_defer_count = 0;
    queue->assist_active = 0;
    queue->assist_done = 0;
    queue->assist_slot_id = UINT16_MAX;
    queue->assist_seq = 0;
    queue->ctrl.queue_state = PVBLK_QUEUE_READY;
    queue->slots = slots + q * queue_depth;

    for (uint32_t i = 0; i < queue_depth; ++i) {
      pvblk_slot_t* slot = &queue->slots[i];
      slot->qid = (uint16_t)q;
      slot->slot_id = (uint16_t)i;
      slot->seq = 1;
      slot->state = PVBLK_REQ_FREE;
      slot->queue = queue;
      slot->ctx = NULL;
      slot->block_op = NULL;
      memset(slot->block_ops, 0, sizeof(slot->block_ops));
    }
  }

  return ZX_OK;
}

void pvblk_guest_release(pvblk_guest_t* guest) {
  if (!guest) {
    return;
  }
  memset(guest, 0, sizeof(*guest));
}

static zx_status_t pvblk_queue_reset_slots_for_attach(pvblk_queue_t* queue) {
  if (!queue || !queue->slots || queue->depth == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (uint32_t i = 0; i < queue->depth; ++i) {
    pvblk_slot_t* slot = &queue->slots[i];
    enum pvblk_req_state state =
        __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
    if (state != PVBLK_REQ_FREE) {
      zxlogf(ERROR,
             "[pvblk]: reject ring attach with busy slot qid=%u slot=%u "
             "state=%u seq=%u\n",
             queue->qid, slot->slot_id, state,
             __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE));
      return ZX_ERR_BAD_STATE;
    }
    for (uint32_t j = 0; j < PVBLK_MAX_INLINE_SEGS; ++j) {
      if (slot->retry_ctx[j]) {
        zxlogf(ERROR,
               "[pvblk]: reject ring attach with retry context qid=%u "
               "slot=%u idx=%u seq=%u\n",
               queue->qid, slot->slot_id, j,
               __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE));
        return ZX_ERR_BAD_STATE;
      }
    }
  }

  for (uint32_t i = 0; i < queue->depth; ++i) {
    pvblk_slot_t* slot = &queue->slots[i];
    slot->op_count = 0;
    slot->pending_count = 0;
    slot->bytes_done = 0;
    slot->aggregate_status = PVBLK_STS_OK;
    slot->block_op = slot->block_ops[0];
    memset(slot->crypto_ctx, 0, sizeof(slot->crypto_ctx));
    memset(&slot->sqe, 0, sizeof(slot->sqe));
    memset(&slot->cqe, 0, sizeof(slot->cqe));
    __atomic_store_n(&slot->seq, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&slot->state, PVBLK_REQ_FREE, __ATOMIC_RELEASE);
  }

  return ZX_OK;
}

zx_status_t pvblk_guest_attach_ring(pvblk_guest_t* guest,
                                    uint16_t qid,
                                    const pvblk_ring_t* ring) {
  pvblk_queue_t* queue = pvblk_get_queue(guest, qid);
  if (!queue) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!pvblk_ring_valid(ring, queue->depth)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = pvblk_queue_reset_slots_for_attach(queue);
  if (status != ZX_OK) {
    return status;
  }

  queue->ring = *ring;
  memset(&queue->ctrl, 0, sizeof(queue->ctrl));
  memset(queue->ring.ctrl, 0, sizeof(*queue->ring.ctrl));
  memset(queue->ring.cq, 0, sizeof(*queue->ring.cq) * queue->depth);
  __atomic_store_n(&queue->irq_arm_mode, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&queue->last_irq_seq, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&queue->completion_signal_defer_count, 0,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&queue->assist_active, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&queue->assist_done, 0, __ATOMIC_RELEASE);
  queue->assist_slot_id = UINT16_MAX;
  queue->assist_seq = 0;
  queue->ctrl.queue_state = PVBLK_QUEUE_READY;
  queue->ring.ctrl->queue_state = PVBLK_QUEUE_READY;
  return ZX_OK;
}

void pvblk_guest_detach_rings(pvblk_guest_t* guest) {
  if (!pvblk_guest_valid(guest)) {
    return;
  }

  for (uint32_t q = 0; q < guest->queue_count; ++q) {
    memset(&guest->queues[q].ring, 0, sizeof(guest->queues[q].ring));
    guest->queues[q].ctrl.queue_state = PVBLK_QUEUE_READY;
  }
}

zx_status_t pvblk_slot_acquire(pvblk_guest_t* guest,
                               const struct pvblk_sqe* sqe,
                               pvblk_slot_t** out_slot) {
  if (!out_slot) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out_slot = NULL;

  if (!sqe || sqe->nr_segments > PVBLK_MAX_INLINE_SEGS) {
    return ZX_ERR_INVALID_ARGS;
  }

  pvblk_queue_t* queue = pvblk_get_queue(guest, sqe->qid);
  if (!queue || sqe->slot >= queue->depth) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  pvblk_slot_t* slot = &queue->slots[sqe->slot & queue->mask];
  if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != PVBLK_REQ_FREE) {
    return ZX_ERR_BAD_STATE;
  }
  if (!pvblk_seq_matches(__atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE),
                         sqe->seq)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  pvblk_sqe_copy_for_slot(&slot->sqe, sqe);
  __atomic_store_n(&slot->state, PVBLK_REQ_SUBMITTED, __ATOMIC_RELEASE);
  *out_slot = slot;
  return ZX_OK;
}

static void pvblk_queue_complete_raw_error(pvblk_queue_t* queue,
                                           const struct pvblk_sqe* sqe);

zx_status_t pvblk_queue_drain(pvblk_guest_t* guest,
                              uint16_t qid,
                              pvblk_slot_t** slots,
                              uint32_t max_slots,
                              uint32_t* out_count) {
  if (!slots || max_slots == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (out_count) {
    *out_count = 0;
  }

  pvblk_queue_t* queue = pvblk_get_queue(guest, qid);
  if (!queue) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!pvblk_ring_valid(&queue->ring, queue->depth)) {
    return ZX_ERR_BAD_STATE;
  }

  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  if (ctrl->queue_state != PVBLK_QUEUE_READY) {
    return ZX_ERR_BAD_STATE;
  }

  uint32_t drained = 0;
  uint32_t sq_head = __atomic_load_n(&ctrl->sq_head, __ATOMIC_ACQUIRE);
  uint32_t sq_tail = __atomic_load_n(&ctrl->sq_tail, __ATOMIC_ACQUIRE);
  while (sq_head != sq_tail && drained < max_slots) {
    uint32_t sq_index = sq_head & queue->mask;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    struct pvblk_sqe sqe;
    pvblk_slot_t* slot = NULL;

    pvblk_sqe_copy_for_slot(&sqe, &queue->ring.sq[sq_index]);
    zx_status_t status = pvblk_slot_acquire(guest, &sqe, &slot);
    if (status != ZX_OK) {
      zxlogf(ERROR,
             "[pvblk][%u]: drain invalid sqe qid=%u slot=%u seq=%u "
             "status=%d head=%u tail=%u\n",
             guest->vmid, sqe.qid, sqe.slot, sqe.seq, status, sq_head,
             sq_tail);
      pvblk_queue_complete_raw_error(queue, &sqe);
      sq_head++;
      __atomic_store_n(&ctrl->sq_head, sq_head, __ATOMIC_RELEASE);
      continue;
    }

    sq_head++;
    __atomic_store_n(&ctrl->sq_head, sq_head, __ATOMIC_RELEASE);
    slots[drained++] = slot;
  }

  if (out_count) {
    *out_count = drained;
  }
  return ZX_OK;
}

zx_status_t pvblk_slot_mark_issued(pvblk_slot_t* slot) {
  enum pvblk_req_state state =
      slot ? __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) : PVBLK_REQ_FREE;
  if (!slot || (state != PVBLK_REQ_SUBMITTED &&
                state != PVBLK_REQ_VALIDATED)) {
    return ZX_ERR_BAD_STATE;
  }
  __atomic_store_n(&slot->state, PVBLK_REQ_ISSUED_UFS, __ATOMIC_RELEASE);
  return ZX_OK;
}

zx_status_t pvblk_slot_complete(pvblk_slot_t* slot,
                                enum pvblk_status status,
                                uint32_t bytes_done,
                                struct pvblk_cqe* out_cqe) {
  if (!slot || !out_cqe ||
      __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
          PVBLK_REQ_ISSUED_UFS) {
    return ZX_ERR_BAD_STATE;
  }

  __atomic_store_n(&slot->state, PVBLK_REQ_COMPLETING, __ATOMIC_RELEASE);
  memset(&slot->cqe, 0, sizeof(slot->cqe));
  slot->cqe.qid = slot->qid;
  slot->cqe.slot = slot->slot_id;
  slot->cqe.seq = slot->seq;
  slot->cqe.status = (uint16_t)status;
  slot->cqe.bytes_done = bytes_done;
  *out_cqe = slot->cqe;
  __atomic_store_n(&slot->state, PVBLK_REQ_DONE, __ATOMIC_RELEASE);
  return ZX_OK;
}

static uint32_t pvblk_next_seq(uint32_t seq) {
  seq++;
  return seq == 0 ? 1 : seq;
}

static void pvblk_slot_free_retry_contexts(pvblk_slot_t* slot);

static void pvblk_queue_complete_raw_error(pvblk_queue_t* queue,
                                           const struct pvblk_sqe* sqe) {
  if (!queue || !sqe || !pvblk_ring_valid(&queue->ring, queue->depth)) {
    return;
  }

  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  uint32_t cq_tail = __atomic_load_n(&ctrl->cq_tail, __ATOMIC_RELAXED);
  uint32_t cq_index = cq_tail & queue->mask;
  struct pvblk_cqe cqe = {
      .qid = sqe->qid,
      .slot = sqe->slot,
      .seq = sqe->seq,
      .status = PVBLK_STS_IOERR,
      .bytes_done = 0,
  };
  queue->ring.cq[cq_index] = cqe;
  __atomic_store_n(&ctrl->cq_tail, cq_tail + 1, __ATOMIC_RELEASE);
}

zx_status_t pvblk_slot_complete_to_ring(pvblk_guest_t* guest,
                                        pvblk_slot_t* slot,
                                        enum pvblk_status status,
                                        uint32_t bytes_done) {
  if (!guest || !slot || !slot->queue ||
      !pvblk_ring_valid(&slot->queue->ring, slot->queue->depth)) {
    return ZX_ERR_BAD_STATE;
  }

  struct pvblk_cqe cqe;
  zx_status_t ret = pvblk_slot_complete(slot, status, bytes_done, &cqe);
  if (ret != ZX_OK) {
    return ret;
  }

  pvblk_queue_t* queue = slot->queue;
  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  uint32_t cq_tail = __atomic_load_n(&ctrl->cq_tail, __ATOMIC_RELAXED);
  uint32_t cq_index = cq_tail & queue->mask;
  uint32_t next_seq;
  queue->ring.cq[cq_index] = cqe;

  /*
   * Recycle the host slot before publishing cq_tail. The guest can reuse the
   * same blk-mq tag immediately after it observes this CQE.
   */
  next_seq = pvblk_next_seq(slot->seq);
  ret = pvblk_slot_recycle(slot, next_seq);
  if (ret != ZX_OK) {
    return ret;
  }

  __atomic_thread_fence(__ATOMIC_RELEASE);
  __atomic_store_n(&ctrl->cq_tail, cq_tail + 1, __ATOMIC_RELEASE);
  return ZX_OK;
}

zx_status_t pvblk_slot_recycle(pvblk_slot_t* slot, uint32_t seq) {
  if (!slot ||
      __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != PVBLK_REQ_DONE) {
    return ZX_ERR_BAD_STATE;
  }
  slot->op_count = 0;
  slot->pending_count = 0;
  slot->bytes_done = 0;
  slot->aggregate_status = PVBLK_STS_OK;
  pvblk_slot_free_retry_contexts(slot);
  if (slot->sqe.flags & PVBLK_SQE_F_USE_CRYPTO) {
    memset(slot->crypto_ctx, 0, sizeof(slot->crypto_ctx));
  }
  pvblk_sqe_clear_for_recycle(&slot->sqe);
  memset(&slot->cqe, 0, sizeof(slot->cqe));
  __atomic_store_n(&slot->seq, seq, __ATOMIC_RELEASE);
  __atomic_store_n(&slot->state, PVBLK_REQ_FREE, __ATOMIC_RELEASE);
  return ZX_OK;
}

void pvblk_dev_init(pvblk_dev_t* dev) {
  if (!dev) {
    return;
  }

  memset(dev, 0, sizeof(*dev));
  dev->selected_gpa_vmid = GRT_VMID_INVALID;
  for (uint32_t i = 0; i < PVBLK_MAX_GUESTS; ++i) {
    mtx_init(&dev->guests[i].lifecycle_lock, mtx_plain);
    pvblk_guest_ctx_reset(&dev->guests[i]);
    dev->guests[i].guest.vmid = i;
  }
}

void pvblk_dev_release(pvblk_dev_t* dev) {
  if (!dev) {
    return;
  }

  for (uint32_t i = 0; i < PVBLK_MAX_GUESTS; ++i) {
    pvblk_guest_ctx_release(&dev->guests[i]);
  }
  dev->block_proto = NULL;
  dev->block_info = NULL;
  dev->block_op_size = 0;
  dev->backend_name[0] = '\0';
  dev->selected_gpa_vmid = GRT_VMID_INVALID;
}

zx_status_t pvblk_dev_set_backend(pvblk_dev_t* dev,
                                  block_protocol_t* bp,
                                  block_info_t* info,
                                  size_t block_op_size,
                                  const char* backend_name) {
  if (!dev || !bp || !info || block_op_size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  dev->block_proto = bp;
  dev->block_info = info;
  dev->block_op_size =
      (block_op_size + sizeof(void*) - 1U) & ~(sizeof(void*) - 1U);
  if (backend_name) {
    snprintf(dev->backend_name, sizeof(dev->backend_name), "%s", backend_name);
  } else {
    dev->backend_name[0] = '\0';
  }

  zxlogf(INFO,
         "[pvblk]: backend ready, block_op_size: %lu, block_size: %u, "
         "blocks: %lu, backend: %s, block_id: %u, wce: %u\n",
         dev->block_op_size, info->block_size, info->block_count,
         dev->backend_name, info->block_id, info->wce);
  return ZX_OK;
}

static enum pvblk_status pvblk_status_from_zx(zx_status_t status) {
  switch (status) {
    case ZX_OK:
      return PVBLK_STS_OK;
    case ZX_ERR_NOT_SUPPORTED:
      return PVBLK_STS_UNSUPP;
    case ZX_ERR_OUT_OF_RANGE:
      return PVBLK_STS_RANGE;
    default:
      return PVBLK_STS_IOERR;
  }
}

typedef struct pvblk_rw_plan {
  uint64_t block_offset;
  uint64_t block_length;
  uint64_t vmo_off;
} pvblk_rw_plan_t;

typedef struct pvblk_completion_signal_policy {
  uint32_t small_backlog;
  uint32_t batch;
  uint32_t backlog;
} pvblk_completion_signal_policy_t;

static pvblk_completion_signal_policy_t
pvblk_default_completion_signal_policy(void) {
  return (pvblk_completion_signal_policy_t){
      .small_backlog = PVBLK_COMPLETION_SIGNAL_SMALL_BACKLOG,
      .batch = PVBLK_COMPLETION_SIGNAL_BATCH,
      .backlog = PVBLK_COMPLETION_SIGNAL_BACKLOG,
  };
}

static pvblk_completion_signal_policy_t
pvblk_eager_read_completion_signal_policy(void) {
  return (pvblk_completion_signal_policy_t){
      .small_backlog = PVBLK_COMPLETION_SIGNAL_EAGER_READ_SMALL_BACKLOG,
      .batch = PVBLK_COMPLETION_SIGNAL_EAGER_READ_BATCH,
      .backlog = PVBLK_COMPLETION_SIGNAL_EAGER_READ_BACKLOG,
  };
}

static bool pvblk_slot_uses_sos_eager_read_policy(
    const pvblk_guest_ctx_t* ctx,
    const pvblk_slot_t* slot) {
  return ctx && slot && slot->sqe.op == PVBLK_OP_READ &&
         slot->sqe.bytes >= PVBLK_EAGER_SIGNAL_READ_MIN_BYTES &&
         ctx->guest.vmid == GRT_BLOCK_SOS_BACKEND_VMID;
}

static bool pvblk_slot_uses_legacy_read_signal(const pvblk_guest_ctx_t* ctx,
                                               const pvblk_slot_t* slot) {
  if (!ctx || !slot || slot->sqe.op != PVBLK_OP_READ ||
      slot->sqe.bytes < PVBLK_LEGACY_SIGNAL_READ_MIN_BYTES) {
    return false;
  }

  return ctx->guest.vmid == GRT_BLOCK_ALPS_BACKEND_VMID;
}

static void pvblk_queue_refresh_irq_arm(pvblk_queue_t* queue,
                                        uint64_t submitted_before,
                                        uint64_t completed_before,
                                        uint32_t drained_count) {
  if (!queue || !pvblk_ring_valid(&queue->ring, queue->depth)) {
    return;
  }

  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  uint32_t irq_seq = __atomic_load_n(&ctrl->irq_seq, __ATOMIC_ACQUIRE);
  if (!(irq_seq & PVBLK_IRQ_ARM_MODE)) {
    __atomic_store_n(&queue->irq_arm_mode, 0, __ATOMIC_RELEASE);
    return;
  }

  uint32_t cq_head = __atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE);
  uint32_t cq_tail = __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE);
  if (cq_head != cq_tail) {
    __atomic_store_n(&queue->irq_arm_mode, 0, __ATOMIC_RELEASE);
    return;
  }

  if (drained_count != 1 || submitted_before != completed_before) {
    __atomic_store_n(&queue->irq_arm_mode, 0, __ATOMIC_RELEASE);
    return;
  }

  __atomic_store_n(&queue->last_irq_seq, irq_seq, __ATOMIC_RELEASE);
  __atomic_store_n(&queue->irq_arm_mode, 1, __ATOMIC_RELEASE);
}

static bool pvblk_queue_cq_empty(pvblk_queue_t* queue) {
  if (!queue || !pvblk_ring_valid(&queue->ring, queue->depth)) {
    return false;
  }

  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  uint32_t cq_head = __atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE);
  uint32_t cq_tail = __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE);
  return cq_head == cq_tail;
}

static bool pvblk_slot_assist_candidate(pvblk_dev_t* dev,
                                        pvblk_guest_ctx_t* ctx,
                                        pvblk_slot_t* slot,
                                        uint64_t submitted_before,
                                        uint64_t completed_before,
                                        uint32_t drained_count) {
  if (!dev || !ctx || !slot || !slot->queue || drained_count != 1 ||
      submitted_before != completed_before ||
      !pvblk_queue_cq_empty(slot->queue)) {
    return false;
  }
  if (__atomic_load_n(&slot->queue->assist_active, __ATOMIC_ACQUIRE)) {
    return false;
  }
  if (slot->sqe.op != PVBLK_OP_READ && slot->sqe.op != PVBLK_OP_WRITE) {
    return false;
  }
  if (!dev->block_info || slot->sqe.bytes != dev->block_info->block_size ||
      slot->sqe.nr_segments != 1) {
    return false;
  }
  return true;
}

static bool pvblk_assist_begin(pvblk_slot_t* slot) {
  if (!slot || !slot->queue) {
    return false;
  }

  pvblk_queue_t* queue = slot->queue;
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&queue->assist_active, &expected, 1, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    return false;
  }

  queue->assist_slot_id = slot->slot_id;
  queue->assist_seq = slot->seq;
  __atomic_store_n(&queue->assist_done, 0, __ATOMIC_RELEASE);
  return true;
}

static bool pvblk_assist_matches(pvblk_queue_t* queue, pvblk_slot_t* slot) {
  return queue && slot &&
         __atomic_load_n(&queue->assist_active, __ATOMIC_ACQUIRE) &&
         queue->assist_slot_id == slot->slot_id &&
         queue->assist_seq == slot->seq;
}

static bool pvblk_assist_mark_done(pvblk_queue_t* queue,
                                   uint16_t slot_id,
                                   uint32_t seq) {
  if (!queue ||
      !__atomic_load_n(&queue->assist_active, __ATOMIC_ACQUIRE) ||
      queue->assist_slot_id != slot_id || queue->assist_seq != seq) {
    return false;
  }
  __atomic_store_n(&queue->assist_done, 1, __ATOMIC_RELEASE);
  return true;
}

static bool pvblk_assist_wait_done(pvblk_queue_t* queue) {
  if (!queue) {
    return false;
  }

  uint64_t ticks_per_second = zx_ticks_per_second();
  uint64_t wait_ticks =
      (ticks_per_second * PVBLK_COMPLETION_ASSIST_USEC) / 1000000ULL;
  if (!wait_ticks) {
    wait_ticks = 1;
  }
  uint64_t deadline = zx_ticks_get() + wait_ticks;

  do {
    if (__atomic_load_n(&queue->assist_done, __ATOMIC_ACQUIRE)) {
      return true;
    }
    __atomic_signal_fence(__ATOMIC_ACQUIRE);
  } while (zx_ticks_get() < deadline);

  return __atomic_load_n(&queue->assist_done, __ATOMIC_ACQUIRE);
}

static bool pvblk_assist_finish(pvblk_queue_t* queue) {
  if (!queue) {
    return false;
  }

  bool done = __atomic_load_n(&queue->assist_done, __ATOMIC_ACQUIRE);
  queue->assist_slot_id = UINT16_MAX;
  queue->assist_seq = 0;
  __atomic_store_n(&queue->assist_done, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&queue->assist_active, 0, __ATOMIC_RELEASE);
  return done;
}

static bool pvblk_queue_should_signal_guest(pvblk_queue_t* queue) {
  if (!queue || !pvblk_ring_valid(&queue->ring, queue->depth)) {
    return true;
  }

  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  uint32_t irq_seq = __atomic_load_n(&ctrl->irq_seq, __ATOMIC_ACQUIRE);
  if (!(irq_seq & PVBLK_IRQ_ARM_MODE)) {
    return true;
  }

  if (!__atomic_load_n(&queue->irq_arm_mode, __ATOMIC_ACQUIRE)) {
    return true;
  }

  uint32_t last_irq_seq =
      __atomic_load_n(&queue->last_irq_seq, __ATOMIC_ACQUIRE);
  while (last_irq_seq != irq_seq) {
    if (__atomic_compare_exchange_n(&queue->last_irq_seq, &last_irq_seq,
                                    irq_seq, false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
      return true;
    }
  }
  return false;
}

static void pvblk_queue_completion_signal_reset(pvblk_queue_t* queue) {
  if (queue) {
    __atomic_store_n(&queue->completion_signal_defer_count, 0,
                     __ATOMIC_RELEASE);
  }
}

static bool pvblk_queue_backlog_signal_due(pvblk_queue_t* queue,
                                           uint32_t backlog,
                                           const pvblk_completion_signal_policy_t* policy) {
  if (!queue) {
    return true;
  }
  uint32_t backlog_threshold = policy && policy->backlog
                                   ? policy->backlog
                                   : PVBLK_COMPLETION_SIGNAL_BACKLOG;
  uint32_t batch = policy && policy->batch
                       ? policy->batch
                       : PVBLK_COMPLETION_SIGNAL_BATCH;
  if (backlog >= backlog_threshold) {
    pvblk_queue_completion_signal_reset(queue);
    return true;
  }
  uint32_t deferred = __atomic_add_fetch(
      &queue->completion_signal_defer_count, 1, __ATOMIC_ACQ_REL);
  if (deferred >= batch) {
    pvblk_queue_completion_signal_reset(queue);
    return true;
  }
  return false;
}

static bool pvblk_queue_completion_needs_signal(
    pvblk_queue_t* queue,
    const pvblk_completion_signal_policy_t* policy) {
  if (!queue || !pvblk_ring_valid(&queue->ring, queue->depth)) {
    return true;
  }

  struct pvblk_queue_ctrl* ctrl = pvblk_queue_ctrl(queue);
  uint32_t cq_head = __atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE);
  uint32_t cq_tail = __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE);
  uint32_t backlog = cq_tail - cq_head;
  uint32_t small_backlog = policy && policy->small_backlog
                               ? policy->small_backlog
                               : PVBLK_COMPLETION_SIGNAL_SMALL_BACKLOG;

  if (backlog == 0) {
    pvblk_queue_completion_signal_reset(queue);
    return false;
  }
  if (backlog <= small_backlog) {
    if (pvblk_queue_should_signal_guest(queue)) {
      pvblk_queue_completion_signal_reset(queue);
      return true;
    }
  }
  return pvblk_queue_backlog_signal_due(queue, backlog, policy);
}

static void pvblk_signal_guest(pvblk_guest_ctx_t* ctx) {
  if (ctx && ctx->completion_event != ZX_HANDLE_INVALID) {
    zx_object_signal(ctx->completion_event, 0, ZX_USER_SIGNAL_0);
  }
}

static bool pvblk_block_op_is_rw(const block_op_t* block_op) {
  if (!block_op) {
    return false;
  }
  uint32_t command = block_op->command & BLOCK_OP_MASK;
  return command == BLOCK_OP_READ || command == BLOCK_OP_WRITE;
}

static void pvblk_put_crypto_keyslot(block_op_t* block_op) {
  if (!pvblk_block_op_is_rw(block_op)) {
    return;
  }

  struct bio_crypt_ctx* crypto_ctx = block_op_crypto_ctx(block_op);
  if (!crypto_ctx) {
    return;
  }
  if (crypto_ctx->keyslot) {
    blk_crypto_put_keyslot(crypto_ctx->keyslot);
    crypto_ctx->keyslot = NULL;
  }
  block_op_crypto_ctx(block_op) = NULL;
}

static void pvblk_release_prepared_block_ops(pvblk_slot_t* slot) {
  if (!slot) {
    return;
  }
  if (!(slot->sqe.flags & PVBLK_SQE_F_USE_CRYPTO)) {
    return;
  }
  for (uint32_t i = 0; i < PVBLK_MAX_INLINE_SEGS; ++i) {
    pvblk_put_crypto_keyslot(slot->block_ops[i]);
  }
}

static void pvblk_slot_free_retry_contexts(pvblk_slot_t* slot) {
  if (!slot) {
    return;
  }
  for (uint32_t i = 0; i < PVBLK_MAX_INLINE_SEGS; ++i) {
    block_retry_free(slot->retry_ctx[i]);
    slot->retry_ctx[i] = NULL;
  }
}

static int pvblk_slot_block_op_index(pvblk_slot_t* slot, block_op_t* block_op) {
  if (!slot || !block_op) {
    return -1;
  }
  uint32_t op_count = slot->op_count;
  if (op_count == 0 || op_count > PVBLK_MAX_INLINE_SEGS) {
    op_count = PVBLK_MAX_INLINE_SEGS;
  }
  for (uint32_t i = 0; i < op_count; ++i) {
    if (slot->block_ops[i] == block_op) {
      return (int)i;
    }
  }
  return -1;
}

static void pvblk_log_backend_error(pvblk_guest_ctx_t* ctx,
                                    pvblk_slot_t* slot,
                                    block_op_t* block_op,
                                    zx_status_t status,
                                    int op_index) {
  if (!ctx || !slot || !block_op || status == ZX_OK) {
    return;
  }
  uint32_t command = block_op->command & BLOCK_OP_MASK;
  if (pvblk_block_op_is_rw(block_op)) {
    zxlogf(ERROR,
           "[pvblk][%u]: backend error qid=%u slot=%u seq=%u idx=%d op=%u "
           "status=%d sq_sector=%" PRIu64 " sq_bytes=%u sq_segs=%u "
           "dev_off=%" PRIu64 " len=%u sector=%" PRIu64 " sectors=%" PRIu64
           " vmo_off=%" PRIu64 " cache_flushed=%u\n",
           ctx->guest.vmid, slot->qid, slot->slot_id, slot->seq, op_index,
           slot->sqe.op, status, slot->sqe.sector, slot->sqe.bytes,
           slot->sqe.nr_segments, block_op->rw.offset_dev,
           block_op->rw.length, block_op->rw.sector, block_op->rw.sectors,
           block_op->rw.offset_vmo, block_op->rw.cache_flushed ? 1U : 0U);
    return;
  }
  zxlogf(ERROR,
         "[pvblk][%u]: backend error qid=%u slot=%u seq=%u idx=%d op=%u "
         "cmd=%u status=%d sq_sector=%" PRIu64 " sq_bytes=%u sq_segs=%u\n",
         ctx->guest.vmid, slot->qid, slot->slot_id, slot->seq, op_index,
         slot->sqe.op, command, status, slot->sqe.sector, slot->sqe.bytes,
         slot->sqe.nr_segments);
}

static zx_status_t pvblk_complete_slot_to_guest(pvblk_guest_ctx_t* ctx,
                                                pvblk_slot_t* slot,
                                                enum pvblk_status status,
                                                uint32_t bytes_done) {
  if (!ctx || !slot) {
    return ZX_ERR_INVALID_ARGS;
  }

  pvblk_queue_t* queue = slot->queue;
  uint16_t slot_id = slot->slot_id;
  uint32_t seq = slot->seq;
  bool assist_completion = pvblk_assist_matches(queue, slot);
  bool sos_eager_read_signal = pvblk_slot_uses_sos_eager_read_policy(ctx, slot);
  bool legacy_read_signal = pvblk_slot_uses_legacy_read_signal(ctx, slot);
  bool needs_signal = false;
  pvblk_completion_signal_policy_t signal_policy =
      sos_eager_read_signal ? pvblk_eager_read_completion_signal_policy()
                            : pvblk_default_completion_signal_policy();
  zx_status_t ret =
      pvblk_slot_complete_to_ring(&ctx->guest, slot, status, bytes_done);
  if (ret != ZX_OK) {
    pvblk_counter_inc(&ctx->error_count);
    goto out_complete;
  }

  if (assist_completion) {
    if (pvblk_assist_mark_done(queue, slot_id, seq)) {
      goto out_complete;
    }
  }
  if (legacy_read_signal) {
    pvblk_queue_completion_signal_reset(queue);
    needs_signal = pvblk_queue_should_signal_guest(queue);
  } else {
    needs_signal = pvblk_queue_completion_needs_signal(queue, &signal_policy);
  }
  if (needs_signal) {
    pvblk_counter_inc(&ctx->guest_signal_count);
    pvblk_signal_guest(ctx);
  } else {
    pvblk_counter_inc(&ctx->guest_signal_suppressed);
  }

out_complete:
  /* This is the final ctx access in the completion path. */
  pvblk_counter_inc(&ctx->completed_count);
  return ret;
}

static void pvblk_block_complete(block_op_t* block_op, zx_status_t status) {
  pvblk_slot_t* slot = block_op ? block_op->cookie : NULL;
  pvblk_guest_ctx_t* ctx = pvblk_slot_ctx(slot);
  if (!ctx || !slot) {
    return;
  }

  int op_index = pvblk_slot_block_op_index(slot, block_op);
  if (op_index < 0) {
    pvblk_log_backend_error(ctx, slot, block_op, ZX_ERR_BAD_STATE, op_index);
    status = status == ZX_OK ? ZX_ERR_BAD_STATE : status;
  } else if (status != ZX_OK) {
    pvblk_log_backend_error(ctx, slot, block_op, status, op_index);
    if (op_index >= 0 && pvblk_ctx_state_load(ctx) < PVBLK_GUEST_STOPPING &&
        ctx->block_ops && ctx->block_ctx &&
        perform_retry_diagnosis(&slot->retry_ctx[op_index], block_op, status,
                                ctx->block_ops, ctx->block_ctx,
                                RETRY_FIXED_DELAY)) {
      return;
    }
  }

  if (op_index >= 0) {
    block_retry_free(slot->retry_ctx[op_index]);
    slot->retry_ctx[op_index] = NULL;
  }

  if (slot->sqe.flags & PVBLK_SQE_F_USE_CRYPTO) {
    pvblk_put_crypto_keyslot(block_op);
  }

  if (slot->op_count > 1) {
    if (status != ZX_OK) {
      enum pvblk_status expected = PVBLK_STS_OK;
      enum pvblk_status failed = pvblk_status_from_zx(status);
      __atomic_compare_exchange_n(&slot->aggregate_status, &expected, failed,
                                  false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    if (__atomic_sub_fetch(&slot->pending_count, 1, __ATOMIC_ACQ_REL) != 0) {
      pvblk_counter_inc(&ctx->multi_op_completion_waits);
      return;
    }
    pvblk_counter_inc(&ctx->multi_op_completion_done);
  }

  uint32_t bytes = slot->sqe.bytes;
  enum pvblk_status pv_status =
      slot->op_count > 1
          ? __atomic_load_n(&slot->aggregate_status, __ATOMIC_ACQUIRE)
          : pvblk_status_from_zx(status);
  uint32_t bytes_done = pv_status == PVBLK_STS_OK ? bytes : 0;
  pvblk_slot_free_retry_contexts(slot);
  zx_status_t ret =
      pvblk_complete_slot_to_guest(ctx, slot, pv_status, bytes_done);
  if (ret != ZX_OK) {
    return;
  }

}

static zx_status_t pvblk_guest_alloc_block_ops(pvblk_dev_t* dev,
                                               pvblk_guest_ctx_t* ctx) {
  if (!dev || !ctx || dev->block_op_size == 0) {
    return ZX_ERR_BAD_STATE;
  }

  ctx->slot_count = ctx->guest.queue_count * ctx->guest.queue_depth;
  ctx->request_stride =
      (dev->block_op_size + sizeof(void*) - 1U) & ~(sizeof(void*) - 1U);
  ctx->requests_buffer = calloc(ctx->slot_count * PVBLK_MAX_INLINE_SEGS,
                                ctx->request_stride);
  if (!ctx->requests_buffer) {
    return ZX_ERR_NO_MEMORY;
  }

  uint8_t* base = ctx->requests_buffer;
  for (uint32_t i = 0; i < ctx->slot_count; ++i) {
    ctx->slots[i].ctx = ctx;
    for (uint32_t j = 0; j < PVBLK_MAX_INLINE_SEGS; ++j) {
      ctx->slots[i].block_ops[j] =
          (block_op_t*)(void*)(base + ((i * PVBLK_MAX_INLINE_SEGS + j) *
                                       ctx->request_stride));
    }
    ctx->slots[i].block_op = ctx->slots[i].block_ops[0];
  }
  return ZX_OK;
}

static zx_status_t pvblk_validate_rw_sqe(pvblk_dev_t* dev,
                                         pvblk_guest_ctx_t* ctx,
                                         const struct pvblk_sqe* sqe,
                                         pvblk_rw_plan_t* plan) {
  if (!dev || !ctx || !sqe || !dev->block_info || !plan ||
      sqe->nr_segments == 0 ||
      sqe->nr_segments > pvblk_supported_max_segs(dev->block_info) ||
      sqe->bytes == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t total = 0;
  for (uint32_t i = 0; i < sqe->nr_segments; ++i) {
    const struct pvblk_segment* seg = &sqe->segs[i];
    if (seg->len == 0 || !pvblk_guest_gpa_to_vaddr(ctx, seg->gpa, seg->len)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    uint64_t seg_vmo_off = seg->gpa >= ctx->vm_phys_mem.start
                               ? seg->gpa - ctx->vm_phys_mem.start
                               : seg->gpa;
    if (seg->len % dev->block_info->block_size ||
        seg_vmo_off % dev->block_info->block_size ||
        (dev->block_info->max_transfer_size &&
         seg->len > dev->block_info->max_transfer_size)) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (seg->len > UINT64_MAX - total) {
      return ZX_ERR_INVALID_ARGS;
    }
    total += seg->len;
  }
  uint64_t vmo_off = sqe->segs[0].gpa >= ctx->vm_phys_mem.start
                         ? sqe->segs[0].gpa - ctx->vm_phys_mem.start
                         : sqe->segs[0].gpa;
  if (total != sqe->bytes || sqe->bytes % dev->block_info->block_size ||
      vmo_off % dev->block_info->block_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t sector_bytes = 0;
  if (!pvblk_sector_to_bytes(sqe->sector, &sector_bytes)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (sector_bytes % dev->block_info->block_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t block_offset = sector_bytes / dev->block_info->block_size;
  uint64_t block_length = sqe->bytes / dev->block_info->block_size;
  uint64_t block_count = pvblk_effective_block_count(dev, ctx);
  if (block_length == 0 || block_offset > block_count ||
      block_length > block_count - block_offset) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  plan->block_offset = block_offset;
  plan->block_length = block_length;
  plan->vmo_off = vmo_off;
  return ZX_OK;
}

static zx_status_t pvblk_prepare_rw_one_block_op(pvblk_dev_t* dev,
                                                 pvblk_guest_ctx_t* ctx,
                                                 pvblk_slot_t* slot,
                                                 uint32_t segment_index,
                                                 uint64_t block_offset,
                                                 uint64_t sector,
                                                 uint32_t bytes,
                                                 uint64_t crypto_byte_offset,
                                                 block_op_t* block_op) {
  struct pvblk_sqe* sqe = &slot->sqe;
  struct pvblk_segment* seg = &sqe->segs[segment_index];
  uint64_t vmo_off = seg->gpa >= ctx->vm_phys_mem.start
                         ? seg->gpa - ctx->vm_phys_mem.start
                         : seg->gpa;

  if (!block_op || bytes == 0 || bytes % dev->block_info->block_size ||
      vmo_off % dev->block_info->block_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  memset(block_op, 0, dev->block_op_size);
  block_op->command = sqe->op == PVBLK_OP_READ ? BLOCK_OP_READ : BLOCK_OP_WRITE;
  block_op->rw.vmo = ctx->gpa_vmo;
  block_op->rw.pages = ctx->mapped_vaddr;
  block_op->rw.length = bytes / dev->block_info->block_size;
  block_op->rw.sector = sector;
  block_op->rw.sectors = bytes / PVBLK_SECTOR_SIZE;
  block_op->rw.offset_dev = block_offset;
  block_op->rw.vm = seg->gpa;
  block_op->rw.vmo_off = vmo_off;
  block_op->rw.offset_vmo = vmo_off / dev->block_info->block_size;
  block_op->rw.cache_flushed = true;
  if (sqe->flags & PVBLK_SQE_F_USE_CRYPTO) {
    if (pvblk_setup_crypto(dev, ctx, slot, block_op, segment_index,
                           crypto_byte_offset) != 0) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  block_op->completion_cb = pvblk_block_complete;
  block_op->cookie = slot;
  return ZX_OK;
}

static bool pvblk_native_sg_rw_candidate(pvblk_dev_t* dev,
                                         pvblk_slot_t* slot,
                                         const pvblk_rw_plan_t* plan) {
  const struct pvblk_sqe* sqe = slot ? &slot->sqe : NULL;
  const block_info_t* info = dev ? dev->block_info : NULL;
  if (!sqe || !info || !plan ||
      (sqe->op != PVBLK_OP_READ && sqe->op != PVBLK_OP_WRITE)) {
    return false;
  }
  if ((sqe->flags & PVBLK_SQE_F_USE_CRYPTO) || sqe->nr_segments <= 1U ||
      sqe->nr_segments > BLOCK_OP_MAX_SG_REGIONS ||
      sqe->bytes > PVBLK_NATIVE_SG_RW_MAX_BYTES) {
    return false;
  }
  if (info->block_size == 0 ||
      plan->block_length > UINT64_MAX / info->block_size) {
    return false;
  }
  return plan->block_length <= UINT32_MAX &&
         sqe->bytes == plan->block_length * info->block_size;
}

static zx_status_t pvblk_prepare_native_sg_rw_block_op(pvblk_dev_t* dev,
                                                       pvblk_guest_ctx_t* ctx,
                                                       pvblk_slot_t* slot,
                                                       const pvblk_rw_plan_t* plan) {
  if (!dev || !ctx || !slot || !plan || !slot->block_ops[0]) {
    return ZX_ERR_INVALID_ARGS;
  }

  struct pvblk_sqe* sqe = &slot->sqe;
  block_info_t* info = dev->block_info;
  block_op_t* block_op = slot->block_ops[0];
  uint64_t first_vmo_off = sqe->segs[0].gpa >= ctx->vm_phys_mem.start
                               ? sqe->segs[0].gpa - ctx->vm_phys_mem.start
                               : sqe->segs[0].gpa;
  uint64_t total_blocks = 0;

  memset(block_op, 0, dev->block_op_size);
  block_op->command = sqe->op == PVBLK_OP_READ ? BLOCK_OP_READ : BLOCK_OP_WRITE;
  block_op->rw.vmo = ctx->gpa_vmo;
  block_op->rw.pages = ctx->mapped_vaddr;
  block_op->rw.length = (uint32_t)plan->block_length;
  block_op->rw.sector = sqe->sector;
  block_op->rw.sectors = sqe->bytes / PVBLK_SECTOR_SIZE;
  block_op->rw.offset_dev = plan->block_offset;
  block_op->rw.vm = sqe->segs[0].gpa;
  block_op->rw.vmo_off = first_vmo_off;
  block_op->rw.offset_vmo = first_vmo_off / info->block_size;
  block_op->rw.cache_flushed = true;
  block_op->rw.sg_count = sqe->nr_segments;

  for (uint32_t i = 0; i < sqe->nr_segments; ++i) {
    struct pvblk_segment* seg = &sqe->segs[i];
    uint64_t vmo_off = seg->gpa >= ctx->vm_phys_mem.start
                           ? seg->gpa - ctx->vm_phys_mem.start
                           : seg->gpa;
    uint64_t seg_blocks = seg->len / info->block_size;
    if (seg_blocks == 0 || seg_blocks > UINT32_MAX ||
        total_blocks > UINT64_MAX - seg_blocks) {
      return ZX_ERR_INVALID_ARGS;
    }
    block_op->rw.sg[i].offset_vmo = vmo_off / info->block_size;
    block_op->rw.sg[i].length = (uint32_t)seg_blocks;
    total_blocks += seg_blocks;
  }

  if (total_blocks != plan->block_length) {
    return ZX_ERR_INVALID_ARGS;
  }

  block_op->completion_cb = pvblk_block_complete;
  block_op->cookie = slot;
  slot->block_op = block_op;
  slot->op_count = 1U;
  return ZX_OK;
}

static bool pvblk_single_rw_fastpath_candidate(const pvblk_dev_t* dev,
                                               const pvblk_slot_t* slot) {
  const struct pvblk_sqe* sqe = slot ? &slot->sqe : NULL;
  const block_info_t* info = dev ? dev->block_info : NULL;
  if (!sqe || !info || (sqe->op != PVBLK_OP_READ && sqe->op != PVBLK_OP_WRITE)) {
    return false;
  }
  if ((sqe->flags & PVBLK_SQE_F_USE_CRYPTO) || sqe->nr_segments != 1U) {
    return false;
  }
  return sqe->bytes == info->block_size && sqe->segs[0].len == sqe->bytes;
}

static zx_status_t pvblk_prepare_single_rw_fastpath(pvblk_dev_t* dev,
                                                    pvblk_guest_ctx_t* ctx,
                                                    pvblk_slot_t* slot) {
  struct pvblk_sqe* sqe = &slot->sqe;
  struct pvblk_segment* seg = &sqe->segs[0];
  block_info_t* info = dev->block_info;
  uint32_t block_size = info->block_size;
  if (!slot->block_ops[0] || block_size == 0 ||
      !pvblk_guest_gpa_to_vaddr(ctx, seg->gpa, seg->len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t vmo_off = seg->gpa >= ctx->vm_phys_mem.start
                         ? seg->gpa - ctx->vm_phys_mem.start
                         : seg->gpa;
  if (vmo_off % block_size || seg->len % block_size ||
      (info->max_transfer_size && seg->len > info->max_transfer_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t sector_bytes = 0;
  if (!pvblk_sector_to_bytes(sqe->sector, &sector_bytes)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (sector_bytes % block_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t block_offset = sector_bytes / block_size;
  uint64_t block_count = pvblk_effective_block_count(dev, ctx);
  if (block_offset >= block_count) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  block_op_t* block_op = slot->block_ops[0];
  memset(block_op, 0, dev->block_op_size);
  block_op->command = sqe->op == PVBLK_OP_READ ? BLOCK_OP_READ : BLOCK_OP_WRITE;
  block_op->rw.vmo = ctx->gpa_vmo;
  block_op->rw.pages = ctx->mapped_vaddr;
  block_op->rw.length = 1U;
  block_op->rw.sector = sqe->sector;
  block_op->rw.sectors = sqe->bytes / PVBLK_SECTOR_SIZE;
  block_op->rw.offset_dev = block_offset;
  block_op->rw.vm = seg->gpa;
  block_op->rw.vmo_off = vmo_off;
  block_op->rw.offset_vmo = vmo_off / block_size;
  block_op->rw.cache_flushed = true;
  block_op->completion_cb = pvblk_block_complete;
  block_op->cookie = slot;
  slot->block_op = block_op;
  slot->op_count = 1U;
  return ZX_OK;
}

static zx_status_t pvblk_prepare_rw_block_op(pvblk_dev_t* dev,
                                             pvblk_guest_ctx_t* ctx,
                                             pvblk_slot_t* slot) {
  struct pvblk_sqe* sqe = &slot->sqe;
  pvblk_rw_plan_t plan;
  if (sqe->op == PVBLK_OP_WRITE &&
      (dev->block_info->flags & BLOCK_FLAG_READONLY)) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if (pvblk_single_rw_fastpath_candidate(dev, slot)) {
    return pvblk_prepare_single_rw_fastpath(dev, ctx, slot);
  }

  zx_status_t status = pvblk_validate_rw_sqe(dev, ctx, sqe, &plan);
  if (status != ZX_OK) {
    return status;
  }
  if (pvblk_native_sg_rw_candidate(dev, slot, &plan)) {
    return pvblk_prepare_native_sg_rw_block_op(dev, ctx, slot, &plan);
  }

  uint64_t next_block = plan.block_offset;
  uint64_t next_sector = sqe->sector;
  uint64_t crypto_byte_offset = 0;
  for (uint32_t i = 0; i < sqe->nr_segments; ++i) {
    if (!slot->block_ops[i]) {
      return ZX_ERR_NO_MEMORY;
    }
    status = pvblk_prepare_rw_one_block_op(
        dev, ctx, slot, i, next_block, next_sector, sqe->segs[i].len,
        crypto_byte_offset, slot->block_ops[i]);
    if (status != ZX_OK) {
      pvblk_release_prepared_block_ops(slot);
      return status;
    }
    next_block += sqe->segs[i].len / dev->block_info->block_size;
    next_sector += sqe->segs[i].len / PVBLK_SECTOR_SIZE;
    crypto_byte_offset += sqe->segs[i].len;
  }

  slot->block_op = slot->block_ops[0];
  slot->op_count = sqe->nr_segments;
  return ZX_OK;
}

static zx_status_t pvblk_prepare_erase_block_op(pvblk_dev_t* dev,
                                                pvblk_guest_ctx_t* ctx,
                                                pvblk_slot_t* slot) {
  struct pvblk_sqe* sqe = &slot->sqe;
  if (dev && dev->block_info &&
      (dev->block_info->flags & BLOCK_FLAG_READONLY)) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if (!dev || !sqe || !dev->block_info || sqe->nr_segments != 0 ||
      !pvblk_feature_supported(dev, sqe->op) || sqe->bytes == 0 ||
      sqe->bytes % dev->block_info->block_size ||
      !pvblk_erase_flags_valid(sqe)) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint64_t max_bytes = pvblk_erase_max_bytes(dev->block_info, sqe->op);
  if (max_bytes != 0 && sqe->bytes > max_bytes) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t sector_bytes = 0;
  if (!pvblk_sector_to_bytes(sqe->sector, &sector_bytes)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint32_t alignment = pvblk_erase_alignment(dev->block_info, sqe->op);
  if (!alignment || sector_bytes % alignment || sqe->bytes % alignment) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t block_offset = sector_bytes / dev->block_info->block_size;
  uint64_t block_length = sqe->bytes / dev->block_info->block_size;
  uint64_t block_count = pvblk_effective_block_count(dev, ctx);
  if (block_length == 0 || block_offset > block_count ||
      block_length > block_count - block_offset) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  block_op_t* block_op = slot->block_ops[0];
  if (!block_op) {
    return ZX_ERR_NO_MEMORY;
  }
  slot->block_op = block_op;

  memset(block_op, 0, dev->block_op_size);
  if (sqe->op == PVBLK_OP_DISCARD) {
    block_op->command = BLOCK_OP_DISCARD;
  } else if (sqe->op == PVBLK_OP_WRITE_ZEROES) {
    block_op->command = BLOCK_OP_WRITE_ZEROES;
  } else if (sqe->op == PVBLK_OP_SECURE_ERASE) {
    block_op->command = BLOCK_OP_SECURE_ERASE;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_op->erase.length = (uint32_t)block_length;
  block_op->erase.offset_dev = block_offset;
  block_op->erase.sector = sqe->sector;
  block_op->erase.sectors = sqe->bytes / PVBLK_SECTOR_SIZE;
  block_op->erase.flags = sqe->flags;
  block_op->completion_cb = pvblk_block_complete;
  block_op->cookie = slot;
  slot->op_count = 1U;
  return ZX_OK;
}

static zx_status_t pvblk_prepare_flush_block_op(pvblk_dev_t* dev,
                                                pvblk_guest_ctx_t* ctx,
                                                pvblk_slot_t* slot) {
  (void)ctx;
  struct pvblk_sqe* sqe = &slot->sqe;
  if (!dev || !sqe || sqe->bytes != 0 || sqe->nr_segments != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  block_op_t* block_op = slot->block_ops[0];
  if (!block_op) {
    return ZX_ERR_NO_MEMORY;
  }
  slot->block_op = block_op;

  memset(block_op, 0, dev->block_op_size);
  block_op->command = BLOCK_OP_FLUSH;
  block_op->completion_cb = pvblk_block_complete;
  block_op->cookie = slot;
  slot->op_count = 1U;
  return ZX_OK;
}

static zx_status_t pvblk_submit_slot(pvblk_dev_t* dev,
                                     pvblk_guest_ctx_t* ctx,
                                     pvblk_slot_t* slot) {
  if (!dev || !ctx || !slot || !dev->block_proto || !dev->block_proto->ops ||
      !dev->block_proto->ops->queue) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status;
  switch (slot->sqe.op) {
    case PVBLK_OP_READ:
    case PVBLK_OP_WRITE:
      status = pvblk_prepare_rw_block_op(dev, ctx, slot);
      break;
    case PVBLK_OP_FLUSH:
      status = pvblk_prepare_flush_block_op(dev, ctx, slot);
      break;
    case PVBLK_OP_DISCARD:
    case PVBLK_OP_WRITE_ZEROES:
    case PVBLK_OP_SECURE_ERASE:
      status = pvblk_prepare_erase_block_op(dev, ctx, slot);
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }

  if (status != ZX_OK) {
    pvblk_release_prepared_block_ops(slot);
    if (pvblk_slot_mark_issued(slot) == ZX_OK) {
      zx_status_t complete_status = pvblk_complete_slot_to_guest(
          ctx, slot, pvblk_status_from_zx(status), 0);
      if (complete_status != ZX_OK) {
        zxlogf(ERROR,
               "[pvblk][%u]: failed to complete rejected request qid=%u "
               "slot=%u seq=%u status=%d complete=%d\n",
               ctx->guest.vmid, slot->qid, slot->slot_id, slot->seq, status,
               complete_status);
      }
    }
    return status;
  }

  status = pvblk_slot_mark_issued(slot);
  if (status != ZX_OK) {
    pvblk_release_prepared_block_ops(slot);
    return status;
  }
  uint32_t op_count = slot->op_count;
  if (op_count == 0 || op_count > PVBLK_MAX_INLINE_SEGS) {
    pvblk_release_prepared_block_ops(slot);
    pvblk_complete_slot_to_guest(ctx, slot, PVBLK_STS_IOERR, 0);
    return ZX_ERR_BAD_STATE;
  }
  __atomic_store_n(&slot->pending_count, op_count, __ATOMIC_RELEASE);
  slot->bytes_done = 0;
  slot->aggregate_status = PVBLK_STS_OK;
  if (pvblk_ctx_state_load(ctx) >= PVBLK_GUEST_STOPPING) {
    pvblk_release_prepared_block_ops(slot);
    pvblk_complete_slot_to_guest(ctx, slot, PVBLK_STS_IOERR, 0);
    return ZX_ERR_BAD_STATE;
  }

  bool any_rgpt = false;
  if (slot->block_op->command != BLOCK_OP_FLUSH &&
      ctx->has_partition_isolation && ctx->vpi_drv) {
    for (uint32_t i = 0; i < op_count; ++i) {
      bool rgpt_flag = false;
      status = vpart_isolate_process(ctx->vpi_drv, slot->block_ops[i],
                                     &rgpt_flag);
      if (status != ZX_OK) {
        pvblk_release_prepared_block_ops(slot);
        pvblk_complete_slot_to_guest(ctx, slot, pvblk_status_from_zx(status), 0);
        return status;
      }
      any_rgpt |= rgpt_flag;
    }
    if (any_rgpt && op_count != 1U) {
      pvblk_release_prepared_block_ops(slot);
      pvblk_complete_slot_to_guest(ctx, slot, PVBLK_STS_UNSUPP, 0);
      return ZX_ERR_NOT_SUPPORTED;
    }
    if (any_rgpt) {
      pvblk_release_prepared_block_ops(slot);
      pvblk_complete_slot_to_guest(ctx, slot, PVBLK_STS_OK, slot->sqe.bytes);
      return ZX_OK;
    }
  }
  pvblk_guest_record_submit_stats(ctx, slot, op_count);
  for (uint32_t i = 0; i < op_count; ++i) {
    dev->block_proto->ops->queue(dev->block_proto->ctx, slot->block_ops[i]);
  }
  return ZX_OK;
}

typedef struct pvblk_fake_backend {
  block_protocol_t proto;
  block_protocol_ops_t ops;
  block_info_t info;
  size_t block_op_size;
  uint32_t queue_count;
  block_op_t* last_op;
  bool defer_completion;
  block_op_t* deferred_op;
} pvblk_fake_backend_t;

static void pvblk_fake_query(void* ctx,
                             block_info_t* info_out,
                             size_t* block_op_size_out) {
  pvblk_fake_backend_t* backend = ctx;
  *info_out = backend->info;
  *block_op_size_out = backend->block_op_size;
}

static void pvblk_fake_queue(void* ctx, block_op_t* op) {
  pvblk_fake_backend_t* backend = ctx;
  backend->queue_count++;
  backend->last_op = op;
  if (backend->defer_completion) {
    backend->deferred_op = op;
    return;
  }
  op->completion_cb(op, ZX_OK);
}

static void pvblk_fake_backend_init(pvblk_fake_backend_t* backend) {
  memset(backend, 0, sizeof(*backend));
  backend->info.block_size = 4096;
  backend->info.block_count = 1024;
  backend->info.max_transfer_size = 4096;
  backend->block_op_size = sizeof(block_op_t);
  backend->ops.query = pvblk_fake_query;
  backend->ops.queue = pvblk_fake_queue;
  backend->proto.ops = &backend->ops;
  backend->proto.ctx = backend;
}

static zx_status_t pvblk_dev_start(pvblk_dev_t* dev,
                                   const void* in_buf,
                                   size_t in_len) {
  if (!dev || !in_buf || in_len != sizeof(struct pvblk_start_param)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const struct pvblk_start_param* param = in_buf;
  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, param->vmid);
  if (!ctx) {
    zxlogf(ERROR, "[pvblk]: invalid start vmid: %u\n", param->vmid);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (param->queue_count == 0 || param->queue_count > PVBLK_MAX_QUEUES ||
      !pvblk_queue_depth_valid(param->queue_depth) ||
      param->mem.end < param->mem.start) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (pvblk_ctx_state_load(ctx) != PVBLK_GUEST_UNINIT) {
    pvblk_guest_ctx_release(ctx);
    ctx->guest.vmid = param->vmid;
  }

  zx_status_t status = pvblk_guest_init(
      &ctx->guest, param->vmid, ctx->queues, param->queue_count, ctx->slots,
      PVBLK_MAX_QUEUES * PVBLK_MAX_QUEUE_DEPTH, param->queue_depth);
  if (status != ZX_OK) {
    return status;
  }

  status = pvblk_guest_alloc_block_ops(dev, ctx);
  if (status != ZX_OK) {
    pvblk_guest_ctx_release(ctx);
    ctx->guest.vmid = param->vmid;
    return status;
  }

  ctx->vm_phys_mem = param->mem;
  ctx->request_count = 0;
  ctx->completed_count = 0;
  ctx->error_count = 0;
  pvblk_guest_stats_reset(ctx);
  ctx->crypto_profile = dev->block_proto && dev->block_proto->ops
                            ? dev->block_proto->ops->crypto_profile
                            : NULL;
  ctx->block_ops = dev->block_proto ? dev->block_proto->ops : NULL;
  ctx->block_ctx = dev->block_proto ? dev->block_proto->ctx : NULL;
  ctx->crypto_key_bitmap = 0;
  memset(ctx->crypto_keys, 0, sizeof(ctx->crypto_keys));
  pvblk_ctx_state_store(ctx, PVBLK_GUEST_STARTED);
  dev->selected_gpa_vmid = param->vmid;

  if (pvblk_backend_needs_partition_isolation(dev)) {
    status = vpart_isolate_create(dev->block_proto, dev->block_info,
                                  dev->block_op_size, param->vmid,
                                  &ctx->vpi_drv, &ctx->has_partition_isolation);
    if (status != ZX_OK) {
      zxlogf(ERROR, "[pvblk][%u]: partition isolation init failed: %d\n",
             param->vmid, status);
      pvblk_guest_ctx_release(ctx);
      ctx->guest.vmid = param->vmid;
      return status;
    }
    zxlogf(INFO, "[pvblk][%u]: partition isolation %s, virtual blocks: %lu\n",
           param->vmid, ctx->has_partition_isolation ? "enabled" : "disabled",
           pvblk_effective_block_count(dev, ctx));
  }

  zxlogf(INFO, "[pvblk][%u]: start, queues: %u, depth: %u, mem: 0x%lx-0x%lx\n",
         param->vmid, param->queue_count, param->queue_depth, param->mem.start,
         param->mem.end);
  return ZX_OK;
}

static zx_status_t pvblk_dev_set_gpa_vmo(pvblk_dev_t* dev,
                                         const void* in_buf,
                                         size_t in_len) {
  if (!dev || !in_buf || in_len != sizeof(zx_handle_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t in_gpa_vmo = *(const zx_handle_t*)in_buf;
  int vmid = dev->selected_gpa_vmid;

  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, (uint16_t)vmid);
  if (!ctx || !pvblk_ctx_started(ctx)) {
    if (in_gpa_vmo != ZX_HANDLE_INVALID) {
      zx_handle_close(in_gpa_vmo);
    }
    return ZX_ERR_BAD_STATE;
  }

  if (ctx->mapped_vaddr && ctx->mapped_size > 0) {
    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)ctx->mapped_vaddr,
                  ctx->mapped_size);
    ctx->mapped_vaddr = NULL;
    ctx->mapped_size = 0;
  }
  if (ctx->gpa_vmo != ZX_HANDLE_INVALID) {
    zx_handle_close(ctx->gpa_vmo);
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
  }

  zx_status_t status =
      zx_handle_duplicate(in_gpa_vmo, ZX_RIGHT_SAME_RIGHTS, &ctx->gpa_vmo);
  zx_handle_close(in_gpa_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[pvblk][%d]: duplicate guest GPA VMO failed: %d\n", vmid,
           status);
    return status;
  }

  uint64_t vmo_size = pvblk_vmo_get_size(ctx->gpa_vmo);
  uint64_t mem_size = pvblk_mem_region_size(&ctx->vm_phys_mem);
  if (vmo_size == UINT64_MAX || vmo_size < mem_size) {
    zxlogf(ERROR,
           "[pvblk][%d]: guest GPA VMO too small, vmo: 0x%lx, mem: 0x%lx\n",
           vmid, vmo_size, mem_size);
    zx_handle_close(ctx->gpa_vmo);
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
    return ZX_ERR_INVALID_ARGS;
  }
  if (vmo_size != mem_size) {
    zxlogf(WARN,
           "[pvblk][%d]: guest GPA VMO size differs, vmo: 0x%lx, mem: 0x%lx\n",
           vmid, vmo_size, mem_size);
  }

  uintptr_t vaddr = 0;
  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ, 0,
                       ctx->gpa_vmo, 0, vmo_size, &vaddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[pvblk][%d]: map guest GPA VMO failed: %d\n", vmid, status);
    zx_handle_close(ctx->gpa_vmo);
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
    return status;
  }

  ctx->mapped_vaddr = (void*)vaddr;
  ctx->mapped_size = vmo_size;
  pvblk_ctx_state_store(ctx, PVBLK_GUEST_MEMORY_READY);
  return ZX_OK;
}

static zx_status_t pvblk_dev_set_completion_event(pvblk_dev_t* dev,
                                                  const void* in_buf,
                                                  size_t in_len) {
  if (!dev || !in_buf || in_len != sizeof(zx_handle_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t in_event = *(const zx_handle_t*)in_buf;
  int vmid = dev->selected_gpa_vmid;
  if (vmid == GRT_VMID_INVALID) {
    return ZX_ERR_BAD_STATE;
  }

  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, (uint16_t)vmid);
  if (!ctx || !pvblk_ctx_started(ctx)) {
    if (in_event != ZX_HANDLE_INVALID) {
      zx_handle_close(in_event);
    }
    return ZX_ERR_BAD_STATE;
  }

  if (ctx->completion_event != ZX_HANDLE_INVALID) {
    zx_handle_close(ctx->completion_event);
    ctx->completion_event = ZX_HANDLE_INVALID;
  }

  zx_status_t status = zx_handle_duplicate(in_event, ZX_RIGHT_SAME_RIGHTS,
                                           &ctx->completion_event);
  zx_handle_close(in_event);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[pvblk][%d]: duplicate completion event failed: %d\n", vmid,
           status);
    return status;
  }
  return ZX_OK;
}

static zx_status_t pvblk_dev_set_ring(pvblk_dev_t* dev,
                                      const void* in_buf,
                                      size_t in_len) {
  if (!dev || !in_buf || in_len != sizeof(struct pvblk_ring_param)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const struct pvblk_ring_param* param = in_buf;
  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, param->vmid);
  if (!ctx || !pvblk_ctx_started(ctx) ||
      pvblk_ctx_state_load(ctx) < PVBLK_GUEST_MEMORY_READY) {
    return ZX_ERR_BAD_STATE;
  }
  if (param->qid >= ctx->guest.queue_count ||
      param->depth != ctx->guest.queue_depth) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t sq_size = sizeof(struct pvblk_sqe) * (uint64_t)param->depth;
  uint64_t cq_size = sizeof(struct pvblk_cqe) * (uint64_t)param->depth;
  pvblk_ring_t ring = {
      .ctrl = pvblk_guest_gpa_to_vaddr(ctx, param->ctrl_gpa,
                                       sizeof(struct pvblk_queue_ctrl)),
      .sq = pvblk_guest_gpa_to_vaddr(ctx, param->sq_gpa, sq_size),
      .cq = pvblk_guest_gpa_to_vaddr(ctx, param->cq_gpa, cq_size),
      .depth = param->depth,
  };
  zx_status_t status = pvblk_guest_attach_ring(&ctx->guest, param->qid, &ring);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t ready_count = 0;
  for (uint32_t q = 0; q < ctx->guest.queue_count; ++q) {
    if (pvblk_ring_valid(&ctx->guest.queues[q].ring, ctx->guest.queue_depth)) {
      ready_count++;
    }
  }
  if (ready_count == ctx->guest.queue_count) {
    pvblk_ctx_state_store(ctx, PVBLK_GUEST_RING_READY);
  }

  zxlogf(INFO, "[pvblk][%u]: ring attached, qid: %u, ready: %u/%u\n",
         param->vmid, param->qid, ready_count, ctx->guest.queue_count);
  return ZX_OK;
}

static zx_status_t pvblk_dev_parse_vmid(const void* in_buf,
                                        size_t in_len,
                                        uint16_t* out_vmid) {
  if (!in_buf || !out_vmid) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (in_len == sizeof(struct pvblk_config_param)) {
    const struct pvblk_config_param* param = in_buf;
    *out_vmid = param->vmid;
    return ZX_OK;
  }
  if (in_len == sizeof(uint16_t)) {
    *out_vmid = *(const uint16_t*)in_buf;
    return ZX_OK;
  }
  if (in_len == sizeof(int32_t)) {
    int32_t vmid = *(const int32_t*)in_buf;
    if (vmid < 0 || vmid > UINT16_MAX) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_vmid = (uint16_t)vmid;
    return ZX_OK;
  }
  return ZX_ERR_INVALID_ARGS;
}

static zx_status_t pvblk_dev_stop(pvblk_dev_t* dev,
                                  const void* in_buf,
                                  size_t in_len) {
  uint16_t vmid = 0;
  zx_status_t status = pvblk_dev_parse_vmid(in_buf, in_len, &vmid);
  if (status != ZX_OK) {
    return status;
  }

  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, vmid);
  if (!ctx) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  pvblk_guest_ctx_release(ctx);
  return ZX_OK;
}

zx_status_t pvblk_dev_get_config(pvblk_dev_t* dev,
                                 uint16_t vmid,
                                 struct pvblk_config* out_config) {
  if (!dev || !dev->block_info || !out_config) {
    return ZX_ERR_INVALID_ARGS;
  }

  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, vmid);
  if (!ctx) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  memset(out_config, 0, sizeof(*out_config));
  uint64_t feature_bits = PVBLK_FEATURE_READ_WRITE | PVBLK_FEATURE_FLUSH |
                          PVBLK_FEATURE_POLL_THEN_IRQ |
                          PVBLK_FEATURE_IRQ_ARM |
                          PVBLK_FEATURE_CONFIG_WCE |
                          PVBLK_FEATURE_TOPOLOGY |
                          PVBLK_FEATURE_SIZE_MAX |
                          PVBLK_FEATURE_SEG_MAX;

  if (pvblk_feature_supported(dev, PVBLK_OP_DISCARD)) {
    feature_bits |= PVBLK_FEATURE_DISCARD;
    out_config->discard_max_sectors =
        pvblk_bytes_to_sectors_limit(dev->block_info->discard_max_bytes);
    out_config->discard_max_segments = PVBLK_SUPPORTED_ERASE_SEGS;
    out_config->discard_granularity = dev->block_info->discard_granularity
                                          ? dev->block_info->discard_granularity
                                          : dev->block_info->block_size;
  }
  if (pvblk_feature_supported(dev, PVBLK_OP_WRITE_ZEROES)) {
    feature_bits |= PVBLK_FEATURE_WRITE_ZEROES;
    out_config->write_zeroes_max_sectors =
        pvblk_bytes_to_sectors_limit(dev->block_info->write_zeroes_max_bytes);
    out_config->write_zeroes_max_segments = PVBLK_SUPPORTED_ERASE_SEGS;
  }
  if (pvblk_feature_supported(dev, PVBLK_OP_SECURE_ERASE)) {
    feature_bits |= PVBLK_FEATURE_SECURE_ERASE;
    out_config->secure_erase_max_sectors =
        pvblk_bytes_to_sectors_limit(dev->block_info->secure_erase_max_bytes);
    out_config->secure_erase_max_segments = PVBLK_SUPPORTED_ERASE_SEGS;
    out_config->secure_erase_granularity =
        dev->block_info->secure_erase_granularity
            ? dev->block_info->secure_erase_granularity
            : dev->block_info->block_size;
  }

  out_config->magic = PVBLK_MAGIC;
  out_config->version = PVBLK_ABI_VERSION;
  out_config->block_size = dev->block_info->block_size;
  out_config->block_count = pvblk_effective_block_count(dev, ctx);
  out_config->queue_count =
      pvblk_ctx_started(ctx) ? ctx->guest.queue_count : PVBLK_MAX_QUEUES;
  out_config->queue_depth =
      pvblk_ctx_started(ctx) ? ctx->guest.queue_depth : PVBLK_MAX_QUEUE_DEPTH;
  out_config->max_segments = pvblk_supported_max_segs(dev->block_info);
  out_config->max_segment_size = dev->block_info->max_transfer_size
                                     ? dev->block_info->max_transfer_size
                                     : UINT32_MAX;
  out_config->wce = dev->block_info->wce ? 1U : 0U;
  out_config->read_only =
      (dev->block_info->flags & BLOCK_FLAG_READONLY) ? 1U : 0U;
  if (out_config->read_only) {
    feature_bits |= PVBLK_FEATURE_READ_ONLY;
  }
  out_config->physical_block_size = dev->block_info->block_size;
  out_config->alignment_offset = 0;
  out_config->io_min = dev->block_info->block_size;
  out_config->io_opt = dev->block_info->max_transfer_size
                           ? dev->block_info->max_transfer_size
                           : dev->block_info->block_size;
  if (dev->block_info->flags & BLOCK_FLAG_BLOCK_ID_VALID) {
    out_config->ufs_lun = dev->block_info->block_id;
    feature_bits |= PVBLK_FEATURE_UFS_LUN;
  }
  if (dev->block_info->inline_crypto_supported) {
    pvblk_crypto_cap_to_wire(&dev->block_info->crypto_cap,
                             &out_config->crypto_cap);
    feature_bits |= PVBLK_FEATURE_INLINE_CRYPTO;
  }
  if (dev->backend_name[0]) {
    snprintf((char*)out_config->serial, sizeof(out_config->serial), "%s",
             dev->backend_name);
    feature_bits |= PVBLK_FEATURE_SERIAL;
  }
  out_config->feature_bits = feature_bits;
  return ZX_OK;
}

zx_status_t pvblk_dev_doorbell(pvblk_dev_t* dev,
                               const struct pvblk_doorbell_param* param) {
  if (!dev || !param) {
    return ZX_ERR_INVALID_ARGS;
  }

  pvblk_guest_ctx_t* ctx = pvblk_dev_get_guest(dev, param->vmid);
  if (!ctx || !pvblk_guest_ctx_begin_doorbell(ctx)) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = ZX_OK;
  if (param->qid >= ctx->guest.queue_count) {
    status = ZX_ERR_OUT_OF_RANGE;
    goto out_doorbell;
  }
  pvblk_queue_t* queue = pvblk_get_queue(&ctx->guest, param->qid);
  uint64_t submitted_before = pvblk_counter_load(&ctx->request_count);
  uint64_t completed_before = pvblk_counter_load(&ctx->completed_count);

  uint32_t budget = param->drain_budget;
  if (budget == 0 || budget > ctx->guest.queue_depth) {
    budget = ctx->guest.queue_depth;
  }

  pvblk_slot_t* drained[PVBLK_MAX_QUEUE_DEPTH];
  uint32_t drained_count = 0;
  status = pvblk_queue_drain(&ctx->guest, param->qid, drained, budget,
                             &drained_count);
  if (status != ZX_OK) {
    pvblk_counter_inc(&ctx->error_count);
    goto out_doorbell;
  }
  pvblk_queue_refresh_irq_arm(queue, submitted_before, completed_before,
                              drained_count);

  for (uint32_t i = 0; i < drained_count; ++i) {
    pvblk_slot_t* slot = drained[i];
    bool assist_enabled = false;
    pvblk_counter_inc(&ctx->request_count);

    if (pvblk_slot_assist_candidate(dev, ctx, slot, submitted_before,
                                    completed_before, drained_count)) {
      assist_enabled = pvblk_assist_begin(slot);
    }

    status = pvblk_submit_slot(dev, ctx, slot);
    if (assist_enabled) {
      pvblk_assist_wait_done(slot->queue);
      pvblk_assist_finish(slot->queue);
    }
    if (status != ZX_OK) {
      pvblk_counter_inc(&ctx->error_count);
      continue;
    }
  }

  status = ZX_OK;

out_doorbell:
  pvblk_guest_ctx_end_doorbell(ctx);
  return status;
}

zx_status_t pvblk_dev_ioctl(pvblk_dev_t* dev,
                            uint32_t op,
                            const void* in_buf,
                            size_t in_len,
                            void* out_buf,
                            size_t out_len,
                            size_t* out_actual) {
  if (out_actual) {
    *out_actual = 0;
  }

  switch (op) {
    case IOCTL_PVBLK_START:
      return pvblk_dev_start(dev, in_buf, in_len);
    case IOCTL_PVBLK_SET_GPA_RANGE:
      return pvblk_dev_set_gpa_vmo(dev, in_buf, in_len);
    case IOCTL_PVBLK_SET_EVENT:
      return pvblk_dev_set_completion_event(dev, in_buf, in_len);
    case IOCTL_PVBLK_SET_RING:
      return pvblk_dev_set_ring(dev, in_buf, in_len);
    case IOCTL_PVBLK_DOORBELL:
      if (in_len != sizeof(struct pvblk_doorbell_param)) {
        return ZX_ERR_INVALID_ARGS;
      }
      return pvblk_dev_doorbell(dev, in_buf);
    case IOCTL_PVBLK_GET_CONFIG: {
      if (!out_buf || out_len < sizeof(struct pvblk_config)) {
        return ZX_ERR_INVALID_ARGS;
      }
      uint16_t vmid = 0;
      zx_status_t status = pvblk_dev_parse_vmid(in_buf, in_len, &vmid);
      if (status != ZX_OK) {
        return status;
      }
      status = pvblk_dev_get_config(dev, vmid, out_buf);
      if (status == ZX_OK && out_actual) {
        *out_actual = sizeof(struct pvblk_config);
      }
      return status;
    }
    case IOCTL_PVBLK_STOP:
      return pvblk_dev_stop(dev, in_buf, in_len);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

typedef struct pvblk_stop_self_check_arg {
  pvblk_dev_t* dev;
  uint16_t vmid;
  zx_status_t status;
} pvblk_stop_self_check_arg_t;

static int pvblk_stop_self_check_thread(void* arg) {
  pvblk_stop_self_check_arg_t* stop_arg = arg;
  stop_arg->status =
      pvblk_dev_stop(stop_arg->dev, &stop_arg->vmid, sizeof(stop_arg->vmid));
  return 0;
}

zx_status_t pvblk_self_check(void) {
  pvblk_guest_t* guest = calloc(1, sizeof(*guest));
  pvblk_queue_t* queues = calloc(1, sizeof(*queues));
  pvblk_slot_t* slots = calloc(PVBLK_MIN_QUEUE_DEPTH, sizeof(*slots));
  pvblk_fake_backend_t* backend = NULL;
  pvblk_dev_t* dev = NULL;
  uint8_t* fake_mem = NULL;
  pvblk_slot_t* slot = NULL;
  struct pvblk_cqe cqe;
  zx_status_t status = ZX_OK;

  if (!guest || !queues || !slots) {
    status = ZX_ERR_NO_MEMORY;
    goto out_basic;
  }

  status = pvblk_guest_init(guest, 7, queues, 1, slots, PVBLK_MIN_QUEUE_DEPTH,
                            PVBLK_MIN_QUEUE_DEPTH);
  if (status != ZX_OK) {
    goto out_basic;
  }

  struct pvblk_sqe sqe = {
      .qid = 0,
      .slot = 0,
      .seq = 1,
      .op = PVBLK_OP_READ,
      .bytes = 4096,
      .sector = 8,
      .nr_segments = 1,
      .segs =
          {
              {
                  .gpa = 0x100000,
                  .len = 4096,
              },
          },
  };

  status = pvblk_slot_acquire(guest, &sqe, &slot);
  if (status != ZX_OK) {
    goto out_basic;
  }

  if (!slot || slot->state != PVBLK_REQ_SUBMITTED) {
    status = ZX_ERR_BAD_STATE;
    goto out_basic;
  }

  status = pvblk_slot_mark_issued(slot);
  if (status != ZX_OK) {
    goto out_basic;
  }

  status = pvblk_slot_complete(slot, PVBLK_STS_OK, sqe.bytes, &cqe);
  if (status != ZX_OK) {
    goto out_basic;
  }
  if (cqe.qid != 0 || cqe.slot != 0 || cqe.seq != 1 ||
      cqe.status != PVBLK_STS_OK || cqe.bytes_done != 4096) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  status = pvblk_slot_recycle(slot, 2);
  if (status != ZX_OK) {
    goto out_basic;
  }

  sqe.seq = 1;
  status = pvblk_slot_acquire(guest, &sqe, &slot);
  if (status != ZX_ERR_IO_DATA_INTEGRITY) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  pvblk_guest_release(guest);

  memset(guest, 0, sizeof(*guest));
  memset(queues, 0, sizeof(*queues));
  memset(slots, 0, sizeof(*slots) * PVBLK_MIN_QUEUE_DEPTH);

  struct pvblk_queue_ctrl ctrl;
  struct pvblk_sqe sq[PVBLK_MIN_QUEUE_DEPTH];
  struct pvblk_cqe cq[PVBLK_MIN_QUEUE_DEPTH];
  pvblk_slot_t* drained[PVBLK_MIN_QUEUE_DEPTH];
  uint32_t drained_count = 0;
  pvblk_ring_t ring = {
      .ctrl = &ctrl,
      .sq = sq,
      .cq = cq,
      .depth = PVBLK_MIN_QUEUE_DEPTH,
  };

  status = pvblk_guest_init(guest, 8, queues, 1, slots, PVBLK_MIN_QUEUE_DEPTH,
                            PVBLK_MIN_QUEUE_DEPTH);
  if (status != ZX_OK) {
    goto out_basic;
  }

  status = pvblk_guest_attach_ring(guest, 0, &ring);
  if (status != ZX_OK) {
    goto out_basic;
  }

  sq[0] = (struct pvblk_sqe){
      .qid = 0,
      .slot = 0,
      .seq = 1,
      .op = PVBLK_OP_READ,
      .bytes = 4096,
      .sector = 16,
      .nr_segments = 1,
      .segs =
          {
              {
                  .gpa = 0x200000,
                  .len = 4096,
              },
          },
  };
  sq[1] = (struct pvblk_sqe){
      .qid = 0,
      .slot = 1,
      .seq = 1,
      .op = PVBLK_OP_WRITE,
      .bytes = 4096,
      .sector = 24,
      .nr_segments = 1,
      .segs =
          {
              {
                  .gpa = 0x300000,
                  .len = 4096,
              },
          },
  };
  ctrl.sq_tail = 2;

  status = pvblk_queue_drain(guest, 0, drained, PVBLK_MIN_QUEUE_DEPTH,
                             &drained_count);
  if (status != ZX_OK || drained_count != PVBLK_MIN_QUEUE_DEPTH ||
      ctrl.sq_head != 2) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  status = pvblk_slot_mark_issued(drained[0]);
  if (status != ZX_OK) {
    goto out_basic;
  }
  status = pvblk_slot_complete_to_ring(guest, drained[0], PVBLK_STS_OK, 4096);
  if (status != ZX_OK) {
    goto out_basic;
  }

  status = pvblk_slot_mark_issued(drained[1]);
  if (status != ZX_OK) {
    goto out_basic;
  }
  status = pvblk_slot_complete_to_ring(guest, drained[1], PVBLK_STS_IOERR, 0);
  if (status != ZX_OK) {
    goto out_basic;
  }

  if (ctrl.cq_tail != 2 || cq[0].slot != 0 || cq[0].seq != 1 ||
      cq[0].status != PVBLK_STS_OK || cq[0].bytes_done != 4096 ||
      cq[1].slot != 1 || cq[1].seq != 1 || cq[1].status != PVBLK_STS_IOERR ||
      cq[1].bytes_done != 0) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  struct pvblk_queue_ctrl* arm_ctrl = queues[0].ring.ctrl;
  arm_ctrl->irq_seq = PVBLK_IRQ_ARM_MODE | 1U;
  queues[0].slots[0].ctx = NULL;
  pvblk_queue_refresh_irq_arm(&queues[0], 0, 0, 1);
  if (queues[0].irq_arm_mode ||
      !pvblk_queue_should_signal_guest(&queues[0])) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  ctrl.cq_head = ctrl.cq_tail;
  pvblk_queue_refresh_irq_arm(&queues[0], 0, 0, 2);
  if (queues[0].irq_arm_mode ||
      !pvblk_queue_should_signal_guest(&queues[0])) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  pvblk_queue_refresh_irq_arm(&queues[0], 0, 0, 1);
  if (!queues[0].irq_arm_mode ||
      pvblk_queue_should_signal_guest(&queues[0])) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  arm_ctrl->irq_seq = PVBLK_IRQ_ARM_MODE | 2U;
  if (!pvblk_queue_should_signal_guest(&queues[0]) ||
      pvblk_queue_should_signal_guest(&queues[0])) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  arm_ctrl->irq_seq = 0;
  queues[0].irq_arm_mode = 0;
  ctrl.cq_head = 0;
  ctrl.cq_tail = 0;
  if (pvblk_queue_completion_needs_signal(&queues[0], NULL)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  ctrl.cq_tail = 1;
  if (!pvblk_queue_completion_needs_signal(&queues[0], NULL)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  ctrl.cq_tail = 2;
  if (!pvblk_queue_completion_needs_signal(&queues[0], NULL)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  queues[0].irq_arm_mode = 1;
  arm_ctrl->irq_seq = PVBLK_IRQ_ARM_MODE | 3U;
  queues[0].last_irq_seq = arm_ctrl->irq_seq;
  pvblk_queue_completion_signal_reset(&queues[0]);
  ctrl.cq_tail = PVBLK_COMPLETION_SIGNAL_SMALL_BACKLOG + 1U;
  for (uint32_t i = 1; i < PVBLK_COMPLETION_SIGNAL_BATCH; ++i) {
    if (pvblk_queue_completion_needs_signal(&queues[0], NULL)) {
      status = ZX_ERR_INTERNAL;
      goto out_basic;
    }
  }
  if (!pvblk_queue_completion_needs_signal(&queues[0], NULL)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  ctrl.cq_tail = PVBLK_COMPLETION_SIGNAL_BACKLOG;
  if (!pvblk_queue_completion_needs_signal(&queues[0], NULL)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  pvblk_completion_signal_policy_t eager_read_policy =
      pvblk_eager_read_completion_signal_policy();
  pvblk_queue_completion_signal_reset(&queues[0]);
  ctrl.cq_tail = PVBLK_COMPLETION_SIGNAL_EAGER_READ_SMALL_BACKLOG + 1U;
  for (uint32_t i = 1; i < PVBLK_COMPLETION_SIGNAL_EAGER_READ_BATCH; ++i) {
    if (pvblk_queue_completion_needs_signal(&queues[0], &eager_read_policy)) {
      status = ZX_ERR_INTERNAL;
      goto out_basic;
    }
  }
  if (!pvblk_queue_completion_needs_signal(&queues[0], &eager_read_policy)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  ctrl.cq_tail = PVBLK_COMPLETION_SIGNAL_EAGER_READ_BACKLOG;
  if (!pvblk_queue_completion_needs_signal(&queues[0], &eager_read_policy)) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  queues[0].irq_arm_mode = 0;
  arm_ctrl->irq_seq = 0;
  ctrl.cq_head = ctrl.cq_tail;

  sq[0].seq = 1;
  ctrl.sq_tail = 3;
  status = pvblk_queue_drain(guest, 0, drained, PVBLK_MIN_QUEUE_DEPTH,
                             &drained_count);
  if (status != ZX_ERR_IO_DATA_INTEGRITY) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  status = pvblk_guest_attach_ring(guest, 0, &ring);
  if (status != ZX_OK) {
    goto out_basic;
  }
  sq[0] = (struct pvblk_sqe){
      .qid = 0,
      .slot = 0,
      .seq = 1,
      .op = PVBLK_OP_READ,
      .bytes = 4096,
      .sector = 32,
      .nr_segments = 1,
      .segs =
          {
              {
                  .gpa = 0x200000,
                  .len = 4096,
              },
          },
  };
  ctrl.sq_tail = 1;
  status = pvblk_queue_drain(guest, 0, drained, PVBLK_MIN_QUEUE_DEPTH,
                             &drained_count);
  if (status != ZX_OK || drained_count != 1 || ctrl.sq_head != 1) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }
  status = pvblk_slot_mark_issued(drained[0]);
  if (status != ZX_OK) {
    goto out_basic;
  }
  status = pvblk_slot_complete_to_ring(guest, drained[0], PVBLK_STS_OK, 4096);
  if (status != ZX_OK) {
    goto out_basic;
  }

  pvblk_guest_detach_rings(guest);
  pvblk_guest_release(guest);

  backend = calloc(1, sizeof(*backend));
  dev = calloc(1, sizeof(*dev));
  fake_mem = calloc(1, 0x4000);
  if (!backend || !dev || !fake_mem) {
    status = ZX_ERR_NO_MEMORY;
    goto out_basic;
  }
  pvblk_fake_backend_init(backend);

  pvblk_dev_init(dev);
  status = pvblk_dev_set_backend(dev, &backend->proto, &backend->info,
                                 backend->block_op_size, "fake");
  if (status != ZX_OK) {
    goto out_basic;
  }

  struct pvblk_start_param start = {
      .vmid = 0,
      .queue_count = 1,
      .queue_depth = PVBLK_MIN_QUEUE_DEPTH,
      .mem =
          {
              .start = 0x400000,
              .end = 0x400000 + 0x3fff,
          },
  };
  status = pvblk_dev_start(dev, &start, sizeof(start));
  if (status != ZX_OK) {
    goto out_basic;
  }

  pvblk_guest_ctx_t* ctx = &dev->guests[0];
  ctx->mapped_vaddr = fake_mem;
  ctx->mapped_size = 0x4000;
  ctx->gpa_vmo = 1;
  pvblk_ctx_state_store(ctx, PVBLK_GUEST_MEMORY_READY);

  struct pvblk_ring_param ring_param = {
      .vmid = 0,
      .qid = 0,
      .depth = PVBLK_MIN_QUEUE_DEPTH,
      .ctrl_gpa = 0x400000,
      .sq_gpa = 0x401000,
      .cq_gpa = 0x402000,
  };
  status = pvblk_dev_set_ring(dev, &ring_param, sizeof(ring_param));
  if (status != ZX_OK) {
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
    ctx->mapped_vaddr = NULL;
    ctx->mapped_size = 0;
    goto out_basic;
  }

  struct pvblk_queue_ctrl* dev_ctrl = (struct pvblk_queue_ctrl*)(void*)fake_mem;
  struct pvblk_sqe* dev_sq = (struct pvblk_sqe*)(void*)(fake_mem + 0x1000);
  struct pvblk_cqe* dev_cq = (struct pvblk_cqe*)(void*)(fake_mem + 0x2000);
  dev_sq[0] = (struct pvblk_sqe){
      .qid = 0,
      .slot = 0,
      .seq = 1,
      .op = PVBLK_OP_READ,
      .bytes = 4096,
      .sector = 0,
      .nr_segments = 1,
      .segs =
          {
              {
                  .gpa = 0x403000,
                  .len = 4096,
              },
          },
  };
  dev_ctrl->sq_tail = 1;

  struct pvblk_doorbell_param doorbell = {
      .vmid = 0,
      .qid = 0,
      .drain_budget = 1,
  };
  status = pvblk_dev_doorbell(dev, &doorbell);
  if (status != ZX_OK || backend->queue_count != 1 || dev_ctrl->sq_head != 1 ||
      dev_ctrl->cq_tail != 1 || dev_cq[0].status != PVBLK_STS_OK ||
      dev_cq[0].bytes_done != 4096 || ctx->request_count != 1 ||
      ctx->completed_count != 1) {
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
    ctx->mapped_vaddr = NULL;
    ctx->mapped_size = 0;
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  /* STOP must wait for a delayed backend completion and reject new doorbells. */
  backend->defer_completion = true;
  dev_sq[1] = dev_sq[0];
  dev_sq[1].seq = 2;
  dev_ctrl->sq_tail = 2;
  status = pvblk_dev_doorbell(dev, &doorbell);
  if (status != ZX_OK || backend->queue_count != 2 || !backend->deferred_op ||
      ctx->request_count != 2 || ctx->completed_count != 1) {
    ctx->gpa_vmo = ZX_HANDLE_INVALID;
    ctx->mapped_vaddr = NULL;
    ctx->mapped_size = 0;
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  /* Keep fake memory owned by the self-check while STOP releases the ctx. */
  ctx->gpa_vmo = ZX_HANDLE_INVALID;
  ctx->mapped_vaddr = NULL;
  ctx->mapped_size = 0;
  pvblk_stop_self_check_arg_t stop_arg = {
      .dev = dev,
      .vmid = 0,
      .status = ZX_ERR_SHOULD_WAIT,
  };
  thrd_t stop_thread;
  if (thrd_create(&stop_thread, pvblk_stop_self_check_thread, &stop_arg) !=
      thrd_success) {
    backend->deferred_op->completion_cb(backend->deferred_op, ZX_OK);
    backend->deferred_op = NULL;
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  bool stopping_seen = false;
  for (uint32_t i = 0; i < 1000; ++i) {
    if (pvblk_ctx_state_load(ctx) == PVBLK_GUEST_STOPPING) {
      stopping_seen = true;
      break;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }
  zx_status_t stopped_doorbell = pvblk_dev_doorbell(dev, &doorbell);
  backend->deferred_op->completion_cb(backend->deferred_op, ZX_OK);
  backend->deferred_op = NULL;
  backend->defer_completion = false;
  int stop_thread_result = 0;
  thrd_join(stop_thread, &stop_thread_result);
  if (!stopping_seen || stopped_doorbell != ZX_ERR_BAD_STATE ||
      stop_thread_result != 0 || stop_arg.status != ZX_OK ||
      pvblk_ctx_state_load(ctx) != PVBLK_GUEST_UNINIT) {
    status = ZX_ERR_INTERNAL;
    goto out_basic;
  }

  status = ZX_OK;

out_basic:
  if (dev) {
    pvblk_dev_release(dev);
  }
  if (guest) {
    pvblk_guest_release(guest);
  }
  free(fake_mem);
  free(dev);
  free(backend);
  free(slots);
  free(queues);
  free(guest);
  return status;
}
