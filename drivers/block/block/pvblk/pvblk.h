// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include <ddk/protocol/block.h>
#include <zircon/device/blk_crypto.h>
#include <zircon/device/pvblk.h>
#include <zircon/types.h>

typedef struct retry_ctx retry_ctx_t;
struct vpart_iso_drv;
typedef struct vpart_iso_drv vpart_iso_drv_t;

enum pvblk_req_state {
  PVBLK_REQ_FREE = 0,
  PVBLK_REQ_SUBMITTED,
  PVBLK_REQ_VALIDATED,
  PVBLK_REQ_ISSUED_UFS,
  PVBLK_REQ_COMPLETING,
  PVBLK_REQ_DONE,
};

typedef struct pvblk_slot {
  uint16_t qid;
  uint16_t slot_id;
  uint32_t seq;
  enum pvblk_req_state state;
  struct pvblk_queue* queue;
  struct pvblk_guest_ctx* ctx;
  block_op_t* block_op;
  block_op_t* block_ops[PVBLK_MAX_INLINE_SEGS];
  uint32_t op_count;
  uint32_t pending_count;
  uint32_t bytes_done;
  enum pvblk_status aggregate_status;
  retry_ctx_t* retry_ctx[PVBLK_MAX_INLINE_SEGS];
  struct bio_crypt_ctx crypto_ctx[PVBLK_MAX_INLINE_SEGS];
  struct pvblk_sqe sqe;
  struct pvblk_cqe cqe;
} pvblk_slot_t;

typedef struct pvblk_ring {
  struct pvblk_queue_ctrl* ctrl;
  struct pvblk_sqe* sq;
  struct pvblk_cqe* cq;
  uint32_t depth;
} pvblk_ring_t;

typedef struct pvblk_queue {
  uint16_t qid;
  uint32_t depth;
  uint32_t mask;
  uint32_t irq_arm_mode;
  uint32_t last_irq_seq;
  uint32_t completion_signal_defer_count;
  uint32_t assist_active;
  uint32_t assist_done;
  uint16_t assist_slot_id;
  uint32_t assist_seq;
  struct pvblk_queue_ctrl ctrl;
  pvblk_slot_t* slots;
  pvblk_ring_t ring;
} pvblk_queue_t;

typedef struct pvblk_guest {
  uint32_t vmid;
  uint32_t queue_count;
  uint32_t queue_depth;
  pvblk_queue_t* queues;
} pvblk_guest_t;

#define PVBLK_MAX_GUESTS 3U
#define PVBLK_BACKEND_NAME_LEN 32U

enum pvblk_guest_state {
  PVBLK_GUEST_UNINIT = 0,
  PVBLK_GUEST_STARTED,
  PVBLK_GUEST_MEMORY_READY,
  PVBLK_GUEST_RING_READY,
  PVBLK_GUEST_STOPPING,
};

typedef struct pvblk_guest_ctx {
  // Serializes the doorbell admission and STOP lifecycle boundary.
  mtx_t lifecycle_lock;
  uint32_t active_doorbells;
  pvblk_guest_t guest;
  pvblk_queue_t queues[PVBLK_MAX_QUEUES];
  pvblk_slot_t slots[PVBLK_MAX_QUEUES * PVBLK_MAX_QUEUE_DEPTH];
  uint32_t slot_count;

  uint32_t state;
  zx_handle_t gpa_vmo;
  struct pvblk_mem_region vm_phys_mem;
  void* mapped_vaddr;
  uint64_t mapped_size;
  void* requests_buffer;
  size_t request_stride;
  zx_handle_t completion_event;
  block_protocol_ops_t* block_ops;
  void* block_ctx;

  uint64_t request_count;
  uint64_t completed_count;
  uint64_t error_count;
  uint64_t read_sqe_segments[PVBLK_MAX_INLINE_SEGS + 1];
  uint64_t write_sqe_segments[PVBLK_MAX_INLINE_SEGS + 1];
  uint64_t read_sqes;
  uint64_t write_sqes;
  uint64_t read_block_ops;
  uint64_t write_block_ops;
  uint64_t read_multi_op_sqes;
  uint64_t write_multi_op_sqes;
  uint64_t read_native_sg_sqes;
  uint64_t read_native_sg_segments;
  uint64_t write_native_sg_sqes;
  uint64_t write_native_sg_segments;
  uint64_t multi_op_completion_waits;
  uint64_t multi_op_completion_done;
  uint64_t guest_signal_count;
  uint64_t guest_signal_suppressed;
  struct blk_crypto_profile* crypto_profile;
  struct blk_crypto_key crypto_keys[PVBLK_CRYPTO_KEY_SLOT_NUM];
  uint64_t crypto_key_bitmap;

  vpart_iso_drv_t* vpi_drv;
  bool has_partition_isolation;
} pvblk_guest_ctx_t;

typedef struct pvblk_dev {
  pvblk_guest_ctx_t guests[PVBLK_MAX_GUESTS];
  block_protocol_t* block_proto;
  block_info_t* block_info;
  size_t block_op_size;
  char backend_name[PVBLK_BACKEND_NAME_LEN];
  int selected_gpa_vmid;
} pvblk_dev_t;

zx_status_t pvblk_guest_init(pvblk_guest_t* guest,
                             uint32_t vmid,
                             pvblk_queue_t* queues,
                             uint32_t queue_count,
                             pvblk_slot_t* slots,
                             uint32_t slot_count,
                             uint32_t queue_depth);

void pvblk_guest_release(pvblk_guest_t* guest);

zx_status_t pvblk_guest_attach_ring(pvblk_guest_t* guest,
                                    uint16_t qid,
                                    const pvblk_ring_t* ring);

void pvblk_guest_detach_rings(pvblk_guest_t* guest);

zx_status_t pvblk_slot_acquire(pvblk_guest_t* guest,
                               const struct pvblk_sqe* sqe,
                               pvblk_slot_t** out_slot);

zx_status_t pvblk_slot_mark_issued(pvblk_slot_t* slot);

zx_status_t pvblk_slot_complete(pvblk_slot_t* slot,
                                enum pvblk_status status,
                                uint32_t bytes_done,
                                struct pvblk_cqe* out_cqe);

zx_status_t pvblk_slot_complete_to_ring(pvblk_guest_t* guest,
                                        pvblk_slot_t* slot,
                                        enum pvblk_status status,
                                        uint32_t bytes_done);

zx_status_t pvblk_slot_recycle(pvblk_slot_t* slot, uint32_t seq);

zx_status_t pvblk_queue_drain(pvblk_guest_t* guest,
                              uint16_t qid,
                              pvblk_slot_t** slots,
                              uint32_t max_slots,
                              uint32_t* out_count);

void pvblk_dev_init(pvblk_dev_t* dev);
void pvblk_dev_release(pvblk_dev_t* dev);

zx_status_t pvblk_dev_set_backend(pvblk_dev_t* dev,
                                  block_protocol_t* bp,
                                  block_info_t* info,
                                  size_t block_op_size,
                                  const char* backend_name);

zx_status_t pvblk_dev_get_config(pvblk_dev_t* dev,
                                 uint16_t vmid,
                                 struct pvblk_config* out_config);

zx_status_t pvblk_dev_doorbell(pvblk_dev_t* dev,
                               const struct pvblk_doorbell_param* param);

zx_status_t pvblk_dev_ioctl(pvblk_dev_t* dev,
                            uint32_t op,
                            const void* in_buf,
                            size_t in_len,
                            void* out_buf,
                            size_t out_len,
                            size_t* out_actual);

zx_status_t pvblk_self_check(void);
