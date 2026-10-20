// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/pci.h>
#include <hw/reg.h>
#include <threads.h>
#include <assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "i2c.h"
#include "i2c_define.h"
#include "mt_i2c.h"
#include "i2c_env.h"
#include "i2c_platform.h"
#include "i2c_func.h"
#include "sync_write.h"
#include <ddk/io-buffer.h>
#include <soc/mt8668/mt8668-hw.h>

struct mtk_i2c_bus bus[MAX_I2C_BUS];
struct mtk_i2c_vmo vmo[MAX_I2C_BUS];
zx_handle_t i2c_bti[MAX_I2C_BUS];
zx_handle_t i2c_dma_handler[MAX_I2C_BUS];

bool i2c_bus_enable[MAX_I2C_BUS] =
    {false, false, true, false, false, true, false, false, false, false, false, false, true};

static void mtk_i2c_dump_data(char *name, const char *data, int size)
{
#ifdef DUMP_I2C_DATA
    int i;

    zxlogf(INFO, "%s:\n", name);
    for (i = 0; i < size; i++) {
        if (!(i % 6))
            zxlogf(INFO, "\n");
        zxlogf(INFO, "%x ", data[i]);
    }
    zxlogf(INFO, "\n");
#endif
}

static void boot_ticks (void) {
    uint64_t freq, tk;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (freq));

    __asm__ volatile("mrs %0, cntvct_el0" : "=r" (tk));

    uint64_t time_s  = tk / freq;
    uint64_t time_ms = (tk * 1000ULL) / freq;
    uint64_t time_us = (tk * 1000000ULL) / freq;

    zxlogf(INFO, "HZ: %lu, tick: %lu, seconds: %lu, ms: %lu, us: %lu\n",
           freq, tk, time_s, time_ms, time_us);
}

// implement device protocol
static void i2c_unbind(void* ctx) {
    i2c_device_t* i2c = ctx;
    //i2c_deinit(i2c);
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    device_remove(i2c->zxdev);
}

static void i2c_release(void* ctx) {
    i2c_device_t* i2c = ctx;

    if (i2c->regs) {
        zx_handle_close(i2c->regs_handle);
        i2c->regs_handle = -1;
    }

    zx_handle_close(i2c->event);

    free(i2c);
}

static void i2c_client_release_transfer_handles(i2c_client_device_t* i2c) {
    if (!i2c || !i2c->data) {
        return;
    }

    if (i2c->data->input_buf_valid) {
        io_buffer_release(&i2c->data->input_buf);
        i2c->data->input_buf_valid = false;
        i2c->data->input_buf_vaddr = NULL;
        i2c->data->input_buf_paddr = 0;
    }

    if (i2c->data->output_buf_valid) {
        io_buffer_release(&i2c->data->output_buf);
        i2c->data->output_buf_valid = false;
        i2c->data->output_buf_vaddr = NULL;
        i2c->data->output_buf_paddr = 0;
    }

    if (i2c->input_buf_addr) {
        zx_vmar_unmap(zx_vmar_root_self(), i2c->input_buf_addr, i2c->data->size);
        i2c->input_buf_addr = 0;
    }

    if (i2c->output_buf_addr) {
        zx_vmar_unmap(zx_vmar_root_self(), i2c->output_buf_addr, i2c->data->size);
        i2c->output_buf_addr = 0;
    }

    if (i2c->input_vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(i2c->input_vmo);
        i2c->input_vmo = ZX_HANDLE_INVALID;
    }

    if (i2c->output_vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(i2c->output_vmo);
        i2c->output_vmo = ZX_HANDLE_INVALID;
    }

    if (i2c->i2c) {
        vmo[i2c->i2c->id].input_vmo = ZX_HANDLE_INVALID;
        vmo[i2c->i2c->id].output_vmo = ZX_HANDLE_INVALID;
        vmo[i2c->i2c->id].size = 0;
    }

    i2c->data->size = 0;
}

static zx_status_t i2c_client_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    i2c_client_device_t* i2c = ctx;
    zx_status_t status = ZX_OK;
    unsigned int speed_khz;

    switch (op) {
        case IOCTL_GRT_I2C_CREATE_TRANSFER_HANDLES: {
			//in buf
			grt_i2c_transfer_t *trans = (grt_i2c_transfer_t *)in_buf;
            i2c_client_release_transfer_handles(i2c);
			i2c->input_vmo = trans->input_vmo;
			i2c->output_vmo = trans->output_vmo;
            i2c->data->size = trans->size;
			
			status = zx_vmar_map(zx_vmar_root_self(),
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0, trans->input_vmo, 0, trans->size, &i2c->input_buf_addr);
            if (status != ZX_OK) {
                zxlogf(ERROR, "i2c: failed to map tx buffer: %d\n", status);
                i2c_client_release_transfer_handles(i2c);
                return status;
            }

			status = io_buffer_init(&i2c->data->input_buf, i2c->i2c->bti, trans->size, IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED);
            if (status != ZX_OK) {
                zxlogf(ERROR, "io_buffer_init tx failed: %d\n", status);
                i2c_client_release_transfer_handles(i2c);
                return status;
            }
            i2c->data->input_buf_valid = true;

			//out buf
			status = zx_vmar_map(zx_vmar_root_self(),
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0, trans->output_vmo, 0, trans->size, &i2c->output_buf_addr);
            if (status != ZX_OK) {
                zxlogf(ERROR, "i2c: failed to map tx buffer: %d\n", status);
                i2c_client_release_transfer_handles(i2c);
                return status;
            }

			status = io_buffer_init(&i2c->data->output_buf, i2c->i2c->bti, trans->size, IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED);
            if (status != ZX_OK) {
                zxlogf(ERROR, "io_buffer_init tx failed: %d\n", status);
                i2c_client_release_transfer_handles(i2c);
                return status;
            }
            i2c->data->output_buf_valid = true;

            vmo[i2c->i2c->id].input_vmo = trans->input_vmo;
            vmo[i2c->i2c->id].output_vmo = trans->output_vmo;
            vmo[i2c->i2c->id].size = trans->size;
            i2c->data->input_buf_vaddr = io_buffer_virt(&i2c->data->input_buf);
            i2c->data->input_buf_paddr = io_buffer_phys(&i2c->data->input_buf);
            i2c->data->output_buf_vaddr = io_buffer_virt(&i2c->data->output_buf);
            i2c->data->output_buf_paddr = io_buffer_phys(&i2c->data->output_buf);

            return ZX_OK;
        }

        case IOCTL_GRT_I2C_TRIGGER_TRANSFER: {
            grt_i2c_transfer_head *trans = (grt_i2c_transfer_head *)in_buf;
            uint64_t incoming_id;
            int speed_khz;
            int hw_status;
            zx_time_t now_ns;

            if (trans == NULL) {
                return ZX_ERR_INVALID_ARGS;
            }

            incoming_id = trans->seq_id;

            if (incoming_id == 0) {
                zxlogf(ERROR, "I2C: invalid sequence id 0\n");
                return ZX_ERR_INVALID_ARGS;
            }

            if (trans->msg_num <= 0) {
                zxlogf(ERROR, "I2C: invalid msg_num=%d, seq=%lu\n",
                    trans->msg_num, (unsigned long)incoming_id);
                return ZX_ERR_INVALID_ARGS;
            }

            mtx_lock(&i2c->i2c->lock);

            now_ns = zx_clock_get(ZX_CLOCK_MONOTONIC);

            if (i2c->i2c->owner_seq_id == incoming_id) {
                i2c->i2c->owner_last_activity_ns = now_ns;
            } else if (i2c->i2c->owner_seq_id != 0 &&
                       i2c->i2c->owner_last_activity_ns != 0 &&
                       now_ns - i2c->i2c->owner_last_activity_ns > ZX_SEC(10)) {
                zxlogf(ERROR, "I2C: seq=%lu triggers zombie reclamation, owner_seq=%lu was inactive for 10s\n",
                    (unsigned long)incoming_id,
                    (unsigned long)i2c->i2c->owner_seq_id);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
            }

            if (i2c->i2c->stop_transfer) {
                zxlogf(ERROR, "I2C: stop_transfer before acquire, owner_seq=%lu, incoming_seq=%lu, msg_count=%d\n",
                    (unsigned long)i2c->i2c->owner_seq_id,
                    (unsigned long)incoming_id,
                    i2c->i2c->msg_count);

                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
                mtx_unlock(&i2c->i2c->lock);
                return ZX_ERR_CANCELED;
            }

            if ((i2c->i2c->msg_count == -1 && i2c->i2c->owner_seq_id != 0) ||
                (i2c->i2c->msg_count != -1 && i2c->i2c->owner_seq_id == 0)) {
                zxlogf(ERROR, "I2C: broken state before acquire, reset. owner_seq=%lu, msg_count=%d\n",
                    (unsigned long)i2c->i2c->owner_seq_id,
                    i2c->i2c->msg_count);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
            }

            if (i2c->i2c->msg_count == -1) {
                i2c->i2c->owner_seq_id = incoming_id;
                i2c->i2c->msg_count = trans->msg_num;
                i2c->i2c->owner_last_activity_ns = zx_clock_get(ZX_CLOCK_MONOTONIC);
                completion_reset(&i2c->i2c->completion);
            } else if (i2c->i2c->owner_seq_id == incoming_id) {
                if (i2c->i2c->msg_count <= 0) {
                    zxlogf(ERROR, "I2C: broken state on continue, owner_seq=%lu, incoming_seq=%lu, msg_count=%d\n",
                        (unsigned long)i2c->i2c->owner_seq_id,
                        (unsigned long)incoming_id,
                        i2c->i2c->msg_count);
                    i2c->i2c->msg_count = -1;
                    i2c->i2c->owner_seq_id = 0;
                    i2c->i2c->owner_last_activity_ns = 0;
                    completion_signal(&i2c->i2c->completion);
                    mtx_unlock(&i2c->i2c->lock);
                    return ZX_ERR_BAD_STATE;
                }

                i2c->i2c->owner_last_activity_ns = zx_clock_get(ZX_CLOCK_MONOTONIC);

            } else {
                mtx_unlock(&i2c->i2c->lock);
                return ZX_ERR_SHOULD_WAIT;
            }

            if (i2c->i2c->owner_seq_id != incoming_id) {
                zxlogf(ERROR, "I2C: owner mismatch after acquire, owner_seq=%lu, incoming_seq=%lu\n",
                    (unsigned long)i2c->i2c->owner_seq_id,
                    (unsigned long)incoming_id);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
                mtx_unlock(&i2c->i2c->lock);
                return ZX_ERR_BAD_STATE;
            }

            if (i2c->i2c->stop_transfer) {
                zxlogf(ERROR, "I2C: stop_transfer after acquire, seq=%lu\n",
                    (unsigned long)incoming_id);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
                mtx_unlock(&i2c->i2c->lock);
                return ZX_ERR_CANCELED;
            }

            i2c->data->head.addr = trans->addr;
            i2c->data->head.bus = trans->bus;
            i2c->data->head.len = trans->len;
            i2c->data->head.flags = trans->flags;
            i2c->data->head.speed = trans->speed;
            i2c->data->head.msg_num = trans->msg_num;
            i2c->data->head.seq_id = trans->seq_id;

            memcpy(i2c->data->input_buf_vaddr, i2c->input_buf_addr, i2c->data->size);
            speed_khz = i2c->data->head.speed / 1000;

            if (i2c->data->head.flags == 0) {
                hw_status = mtk_i2c_write(i2c->data->head.bus,
                                        (unsigned char)i2c->data->head.addr,
                                        speed_khz,
                                        (unsigned char *)i2c->data->input_buf_vaddr,
                                        (unsigned char *)i2c->data->input_buf_paddr,
                                        i2c->data->head.len);
            } else if (i2c->data->head.flags == 1) {
                hw_status = mtk_i2c_read(i2c->data->head.bus,
                                        (unsigned char)i2c->data->head.addr,
                                        speed_khz,
                                        (unsigned char *)i2c->data->output_buf_vaddr,
                                        (unsigned char *)i2c->data->output_buf_paddr,
                                        i2c->data->head.len);
            } else {
                hw_status = mtk_i2c_write_read(i2c->data->head.bus,
                                                (unsigned char)i2c->data->head.addr,
                                                speed_khz,
                                                (unsigned char *)i2c->data->input_buf_vaddr,
                                                (unsigned char *)i2c->data->input_buf_paddr,
                                                (unsigned char *)i2c->data->output_buf_vaddr,
                                                (unsigned char *)i2c->data->output_buf_paddr,
                                                i2c->data->head.len,
                                                i2c->data->head.len);
            }

            i2c->i2c->owner_last_activity_ns = zx_clock_get(ZX_CLOCK_MONOTONIC);

            memcpy(i2c->output_buf_addr, i2c->data->output_buf_vaddr, i2c->data->size);

            if (hw_status < 0) {
                zxlogf(ERROR, "I2C: owner change seq=%lu/rem=%d -> idle, reason=transfer_failed, status=%d\n",
                    (unsigned long)i2c->i2c->owner_seq_id,
                    i2c->i2c->msg_count,
                    hw_status);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
                mtx_unlock(&i2c->i2c->lock);
                return hw_status;
            }

            i2c->i2c->msg_count--;

            if (i2c->i2c->msg_count < 0) {
                zxlogf(ERROR, "I2C: owner change seq=%lu/rem=%d -> idle, reason=negative_msg_count\n",
                    (unsigned long)i2c->i2c->owner_seq_id,
                    i2c->i2c->msg_count);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
                mtx_unlock(&i2c->i2c->lock);
                return ZX_ERR_BAD_STATE;
            }

            if (i2c->i2c->msg_count > 0) {
                zxlogf(INFO, "I2C: seq=%lu waiting next ioctl of same seq, owner_seq=%lu, remaining=%d\n",
                    (unsigned long)incoming_id,
                    (unsigned long)i2c->i2c->owner_seq_id,
                    i2c->i2c->msg_count);
            }

            if (i2c->i2c->msg_count == 0) {
                zxlogf(INFO, "I2C: owner change seq=%lu/rem=0 -> idle, reason=transaction_complete\n",
                    (unsigned long)incoming_id);
                i2c->i2c->msg_count = -1;
                i2c->i2c->owner_seq_id = 0;
                i2c->i2c->owner_last_activity_ns = 0;
                completion_signal(&i2c->i2c->completion);
            }

            mtx_unlock(&i2c->i2c->lock);
            return hw_status;
        }

        /*
         * IOCTL_GRT_I2C_BATCH_TRANSFER
         * ----------------------------
         * Process an entire FE virtio request (one or more i2c_msgs that
         * belong to the same transaction) in a single ioctl. This:
         *   - removes the per-msg ioctl round-trip cost (1 hypercall per
         *     batch vs N before),
         *   - lets the BE call into mtk_i2c_* exactly once per FE request,
         *   - eliminates the legacy owner_seq_id / msg_count zombie machinery
         *     because the whole transaction is atomic w.r.t. i2c->lock.
         *
         * Wire format (in_buf):
         *   grt_i2c_batch_head_t head;
         *   grt_i2c_msg_desc_t   descs[head.msg_num];
         *
         * Shared buffers:
         *   input_buf_vaddr  : packed write data + zero-padding for read slots
         *   output_buf_vaddr : will be filled with read data; left zero for
         *                      write slots.
         *
         * Return:
         *   ZX_OK on full success, the caller still inspects out_buf for
         *   per-msg accounting via grt_i2c_batch_in_hdr_t.
         *   ZX_ERR_* for hard failures.
         */
        case IOCTL_GRT_I2C_BATCH_TRANSFER: {
            const grt_i2c_batch_head_t *bh = (const grt_i2c_batch_head_t *)in_buf;
            grt_i2c_batch_in_hdr_t resp = {};
            const grt_i2c_msg_desc_t *descs;
            size_t expected_in_len;
            uint16_t bus, msg_num;
            uint32_t total_len, speed_khz;
            int hw_status = 0;
            uint32_t i;

            if (in_buf == NULL || out_buf == NULL ||
                out_len < sizeof(resp)) {
                return ZX_ERR_INVALID_ARGS;
            }

            if (in_len < sizeof(*bh)) {
                zxlogf(ERROR, "I2C BATCH: in_len %zu < head\n", in_len);
                return ZX_ERR_INVALID_ARGS;
            }

            bus       = bh->bus;
            msg_num   = bh->msg_num;
            total_len = bh->total_len;

            if (msg_num == 0 || msg_num > GRT_I2C_BATCH_MAX_MSGS) {
                zxlogf(ERROR, "I2C BATCH: bad msg_num=%u, seq=%u\n",
                    msg_num, bh->seq_id);
                return ZX_ERR_INVALID_ARGS;
            }

            expected_in_len = sizeof(*bh) +
                              (size_t)msg_num * sizeof(grt_i2c_msg_desc_t);
            if (in_len < expected_in_len) {
                zxlogf(ERROR, "I2C BATCH: in_len %zu < expected %zu, seq=%u\n",
                    in_len, expected_in_len, bh->seq_id);
                return ZX_ERR_INVALID_ARGS;
            }

            if (total_len > GRT_I2C_BATCH_MAX_TOTAL_LEN ||
                total_len > i2c->data->size) {
                zxlogf(ERROR,
                    "I2C BATCH: total_len %u exceeds limit (max %u, shared=%u), seq=%u\n",
                    total_len, GRT_I2C_BATCH_MAX_TOTAL_LEN,
                    (unsigned)i2c->data->size, bh->seq_id);
                return ZX_ERR_INVALID_ARGS;
            }

            descs = (const grt_i2c_msg_desc_t *)((const uint8_t *)in_buf +
                                                  sizeof(*bh));

            /*
             * Validate every descriptor: addr range, len bounds, and the
             * declared buf_off/len must stay inside the shared input/output
             * buffer. This prevents a malicious or buggy VMM from causing
             * out-of-bounds memcpy in the driver.
             */
            for (i = 0; i < msg_num; i++) {
                uint64_t end = (uint64_t)descs[i].buf_off + descs[i].len;
                if (end > total_len) {
                    zxlogf(ERROR,
                        "I2C BATCH: desc[%u] off=%u len=%u out of bounds (total=%u), seq=%u\n",
                        i, descs[i].buf_off, descs[i].len,
                        total_len, bh->seq_id);
                    return ZX_ERR_INVALID_ARGS;
                }
            }

            speed_khz = bh->speed / 1000;
            if (speed_khz == 0)
                speed_khz = 100; /* fallback to standard mode */

            /*
             * Defensive: every buffer touched below MUST be backed by
             * a valid mapping, otherwise we will fault inside memcpy and
             * crash the whole BE. The shared input/output buffers were
             * sized to i2c->data->size at CREATE_TRANSFER_HANDLES time,
             * so any total_len > that value is fatal regardless of the
             * batch protocol limit.
             */
            if (total_len > i2c->data->size) {
                zxlogf(ERROR,
                    "I2C BATCH: total_len %u > shared %u (seq=%u). VMM/BE buffer size mismatch?\n",
                    total_len, (unsigned)i2c->data->size, bh->seq_id);
                return ZX_ERR_BUFFER_TOO_SMALL;
            }
            if (i2c->data->input_buf_vaddr == NULL ||
                i2c->data->output_buf_vaddr == NULL) {
                zxlogf(ERROR,
                    "I2C BATCH: shared buffers not initialized "
                    "(in_va=%p out_va=%p), did CREATE_TRANSFER_HANDLES run? seq=%u\n",
                    i2c->data->input_buf_vaddr,
                    i2c->data->output_buf_vaddr, bh->seq_id);
                return ZX_ERR_BAD_STATE;
            }
            if (i2c->input_buf_addr == 0 || i2c->output_buf_addr == 0) {
                zxlogf(ERROR,
                    "I2C BATCH: guest VMO not mapped "
                    "(in_guest=0x%lx out_guest=0x%lx) seq=%u\n",
                    (unsigned long)i2c->input_buf_addr,
                    (unsigned long)i2c->output_buf_addr, bh->seq_id);
                return ZX_ERR_BAD_STATE;
            }

            mtx_lock(&i2c->i2c->lock);

            if (i2c->i2c->stop_transfer) {
                zxlogf(ERROR, "I2C BATCH: stop_transfer set, abort seq=%u\n",
                    bh->seq_id);
                mtx_unlock(&i2c->i2c->lock);
                return ZX_ERR_CANCELED;
            }

            /*
             * Bring entire batch input into the BE-side contiguous buffer.
             * The driver expects a contiguous, DMA-mapped region, which
             * is precisely what input_buf provides.
             *
             * IMPORTANT: i2c->data->input_buf was created with
             * IO_BUFFER_UNCACHED. On ARM64, libc memcpy uses unaligned
             * NEON loads/stores that are illegal against device-/strongly-
             * ordered memory and fault even on small lengths (we observed
             * a fault on len=6 with both endpoints page-aligned).
             *
             * We therefore copy in two phases:
             *   1) all complete 8-byte words via aligned uint64_t writes
             *   2) the trailing 0..7 bytes byte-by-byte
             * Both endpoints come from page-granular mappings so the start
             * is always 8-byte aligned; the for-loop below stays inside
             * total_len so it cannot overflow the FE-allocated buffer.
             *
             * Skip when total_len == 0.
             */
            if (total_len > 0) {
                {
                    const uint8_t *src8 = (const uint8_t *)
                        (uintptr_t)i2c->input_buf_addr;
                    uint8_t       *dst8 = (uint8_t *)i2c->data->input_buf_vaddr;
                    uint8_t       *zout = (uint8_t *)i2c->data->output_buf_vaddr;
                    uint32_t whole = total_len & ~7u;
                    uint32_t tail  = total_len - whole;
                    uint32_t k;

                    /* aligned 64-bit copy (and zero) for the bulk */
                    for (k = 0; k < whole; k += 8) {
                        *(uint64_t *)(dst8 + k) =
                            *(const uint64_t *)(src8 + k);
                        *(uint64_t *)(zout + k) = 0;
                    }
                    /* byte tail */
                    for (k = 0; k < tail; k++) {
                        dst8[whole + k] = src8[whole + k];
                        zout[whole + k] = 0;
                    }
                }
            }

            /*
             * Optimisation: a single (write, read) pair to the same address
             * maps to a real i2c repeated-START transaction. Use the
             * dedicated mtk_i2c_write_read entry point so the HW issues it
             * as one transaction.
             */
            if (msg_num == 2 &&
                !(descs[0].flags & GRT_I2C_MSG_FLAG_RD) &&
                 (descs[1].flags & GRT_I2C_MSG_FLAG_RD) &&
                descs[0].addr == descs[1].addr) {

                unsigned char *win  = (unsigned char *)i2c->data->input_buf_vaddr +
                                      descs[0].buf_off;
                unsigned char *wpin = (unsigned char *)i2c->data->input_buf_paddr +
                                      descs[0].buf_off;
                unsigned char *rout = (unsigned char *)i2c->data->output_buf_vaddr +
                                      descs[1].buf_off;
                unsigned char *rpout= (unsigned char *)i2c->data->output_buf_paddr +
                                      descs[1].buf_off;

                hw_status = mtk_i2c_write_read(bus,
                                               (unsigned char)descs[0].addr,
                                               speed_khz,
                                               win, wpin,
                                               rout, rpout,
                                               descs[0].len, descs[1].len);

                if (hw_status >= 0) {
                    resp.completed_msgs = 2;
                } else {
                    zxlogf(ERROR,
                        "I2C BATCH WR: write_read failed, seq=%u status=%d\n",
                        bh->seq_id, hw_status);
                }
            } else {
                /*
                 * General path: loop over msgs and call the matching
                 * single-direction primitive. Still 1 hypercall total.
                 */
                for (i = 0; i < msg_num; i++) {
                    unsigned char addr  = (unsigned char)descs[i].addr;
                    uint32_t      off   = descs[i].buf_off;
                    uint32_t      len   = descs[i].len;
                    int           rc;

                    if (descs[i].flags & GRT_I2C_MSG_FLAG_RD) {
                        rc = mtk_i2c_read(bus, addr, speed_khz,
                            (unsigned char *)i2c->data->output_buf_vaddr + off,
                            (unsigned char *)i2c->data->output_buf_paddr + off,
                            len);
                    } else {
                        rc = mtk_i2c_write(bus, addr, speed_khz,
                            (unsigned char *)i2c->data->input_buf_vaddr + off,
                            (unsigned char *)i2c->data->input_buf_paddr + off,
                            len);
                    }

                    if (rc < 0) {
                        zxlogf(ERROR,
                            "I2C BATCH: msg[%u] failed, seq=%u rc=%d (completed=%u/%u)\n",
                            i, bh->seq_id, rc, resp.completed_msgs, msg_num);
                        hw_status = rc;
                        break;
                    }
                    resp.completed_msgs++;
                }
            }

            /*
             * Publish read results back to the VMM-visible output_buf.
             * Same alignment constraint as copy_in: i2c->data->output_buf
             * is uncached, so use aligned 64-bit copy + byte tail rather
             * than libc memcpy.
             */
            if (total_len > 0) {
                {
                    const uint8_t *src8 = (const uint8_t *)i2c->data->output_buf_vaddr;
                    uint8_t       *dst8 = (uint8_t *)(uintptr_t)i2c->output_buf_addr;
                    uint32_t whole = total_len & ~7u;
                    uint32_t tail  = total_len - whole;
                    uint32_t k;

                    for (k = 0; k < whole; k += 8) {
                        *(uint64_t *)(dst8 + k) =
                            *(const uint64_t *)(src8 + k);
                    }
                    for (k = 0; k < tail; k++)
                        dst8[whole + k] = src8[whole + k];
                }
            }

            mtx_unlock(&i2c->i2c->lock);

            resp.status = (resp.completed_msgs == msg_num) ?
                          0 /* VIRTIO_I2C_MSG_OK */ :
                          1 /* VIRTIO_I2C_MSG_ERR */;

            memcpy(out_buf, &resp, sizeof(resp));
            if (out_actual)
                *out_actual = sizeof(resp);

            return (resp.status == 0) ? ZX_OK :
                   (hw_status < 0 ? hw_status : ZX_ERR_IO);
        }

        case IOCTL_GRT_I2C_CLOSE_TRANSFER_HANDLES: {
            i2c_client_release_transfer_handles(i2c);
            return ZX_OK;
        }
        case IOCTL_GRT_I2C_GET_EVENT: {

            return ZX_OK;
        }
        case IOCTL_GRT_STOP_I2C_DMA_TRANSFER: {

            return ZX_OK;
        }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t i2c_client_close(void* ctx, uint32_t flags) {
    struct i2c_client_device* s = ctx;
    i2c_client_release_transfer_handles(s);

    if (s->data) {
        free(s->data);
        s->data = NULL;
    }

    free(ctx);
    return ZX_OK;
}

static zx_protocol_device_t client_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = i2c_client_ioctl,
    .close = i2c_client_close,
};

static zx_status_t i2c_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {

    struct i2c_client_device* s = calloc(1, sizeof(struct i2c_client_device));

    s->i2c = ctx;

    grt_i2c_data_t *i2c_data = calloc(1, sizeof(grt_i2c_data_t));

    s->data = i2c_data;

    device_add_args_t i2c_client_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vm-i2c",
        .ctx = s,
        .ops = &client_device_proto,
        .flags = DEVICE_ADD_INSTANCE,
    };

    zx_status_t status = device_add(s->i2c->zxdev, &i2c_client_args, &s->device);
    if (status != ZX_OK) {
        free(s);
        return status;
    }
    *dev_out = s->device;
    return ZX_OK;
}

static zx_protocol_device_t i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = i2c_unbind,
    .release = i2c_release,
    .open = i2c_open,
};


void i2c_irq_handler(i2c_device_t* dev) {
	zx_status_t status;
	uint32_t reg_val;

	mtx_lock(&dev->irq_lock);

	status = zx_object_signal(dev->dma_event, 0, ZX_USER_SIGNAL_0);
	if (status != ZX_OK) {
		zxlogf(ERROR,"zx_object_signal failed: %d\n", status);
	}

	mtx_unlock(&dev->irq_lock);
	return;
}

static zx_status_t i2c_irq_thread(void* arg) {
	zx_status_t wait_ret;
	i2c_device_t* dev = (i2c_device_t*)arg;

	while (true) {
		uint64_t slots;
		wait_ret = zx_interrupt_wait(dev->irq_handle, &slots);
		if (wait_ret != ZX_OK) {
				zxlogf(ERROR, "i2c: ======zx_interrupt_wait failed=====>wait_ret:%d\n", wait_ret);
			break;
		}
		// handle irq
		i2c_irq_handler(dev);
	}
	return ZX_OK;
}

static zx_status_t i2c_irq_init(i2c_device_t *dev) {
	zx_status_t status;
	uint32_t reg_val;

	status = pdev_map_interrupt(&dev->pdev, 0, &dev->irq_handle);
	if (status != ZX_OK) {
		zxlogf(ERROR, "i2c: pdev_map_interrupt failed\n");
		return status;
	}

	pdev_get_irq_info(&dev->pdev, 0, &dev->out_irq);
	dev->irq = dev->out_irq.irq;
	zxlogf(INFO, "i2c interrupt:%u\n", dev->irq);

	status = zx_interrupt_unmask(dev->irq_handle, dev->irq);
	if (status != ZX_OK) {
		zxlogf(ERROR, "i2c: failed to unmask irq\n");
		return status;
	}

	status = thrd_create_with_name(&dev->irq_thread, i2c_irq_thread, dev,
									"i2c-irq");
	if (status != thrd_success) {
		zxlogf(ERROR, "i2c: failed to create irq thread\n");
		zx_handle_close(dev->irq_handle);
		return status;
	}
	thrd_detach(dev->irq_thread);

	return ZX_OK;
}

// implement driver object:
static zx_status_t i2c_bind(void* ctx, zx_device_t* dev) {
    zx_status_t status;
	const char* name;
	int id;

    // map resources and initialize the device
    i2c_device_t* device = calloc(1, sizeof(i2c_device_t));
    if (!device)
        return ZX_ERR_NO_MEMORY;

    if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV,
            (void*) &device->pdev))
        return ZX_ERR_NOT_SUPPORTED;

	// get bus id from name
	name = device_get_name(dev);

	if (sscanf(name, "i2c%d", &id) != 1) {
		zxlogf(ERROR, "i2c: could not get device id\n");
		return ZX_ERR_NO_MEMORY;
	}
    device->id = id;

    if (!i2c_bus_enable[id]){
        zxlogf(ERROR, "i2c: disable id is %d\n", id);
        i2c_release(device);
        return ZX_OK;
    }

    status = pdev_get_bti(&device->pdev, 0, &device->bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c: could not get BTI handle: %d\n", status);
        goto fail;
    }

    i2c_bti[id] = device->bti;

    // map register window
    status = pdev_map_mmio(&device->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->regs, &device->regs_size,
                              &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c: failed to map base address: %d\n", status);
        goto fail;
    }

    status = pdev_map_mmio(&device->pdev, 1, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &device->dma_regs, &device->regs_size,
                              &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c: failed to map dma address: %d\n", status);
        goto fail;
    }

	if ((status = zx_event_create(0, &device->event)) < 0) {
		goto fail;
	}

	//create dma event
	if ((status = zx_event_create(0, &device->dma_event)) < 0) {
		zxlogf(ERROR, "cannot create dma event: %d\n", status);
		goto fail;
	}

    i2c_dma_handler[id] = device->dma_event;

	status = i2c_irq_init(device);
	if (status != ZX_OK) {
		zxlogf(ERROR, "i2c: init interrupt error: %d\n", status);
		goto fail;
	}

    // create and add the i2c (char) device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "mtk_i2c",
        .ctx = device,
        .ops = &i2c_device_proto,
        .flags = DEVICE_ADD_MUST_ISOLATE,
    };

    status = device_add(dev, &args, &device->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    bus[id].base = device->regs;
    bus[id].pdma_base = device->dma_regs;

    device->stop_transfer = false;
    device->msg_count = -1;
    device->owner_seq_id = 0;
    mtx_init(&device->lock, mtx_plain);
	mtx_init(&device->irq_lock, mtx_plain);

    return ZX_OK;

fail:
    i2c_release(device);
    return status;
}

static zx_driver_ops_t i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = i2c_bind,
};

ZIRCON_DRIVER_BEGIN(i2c, i2c_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8668),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_I2C),
ZIRCON_DRIVER_END(i2c)
