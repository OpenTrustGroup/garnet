// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trusty_std.h>
#include "ufs_core.h"
#include "ufs_interface.h"
#include "ufs_utils.h"
#include "ufs_sys.h"
#include "ufs_platform.h"
#include "virtio_rpmb.h"

#include "garnet/public/lib/blk_crypto/blk_crypto.h"

#define VBLOCK_DRV_THREAD_PRIORITY  17
#define PERF_BIND_CPU_TEMPORARILY 1
#define VBLOCK_DRV_ALLOC 8192
#define UFS_BLOCK_MAX_SEG_NUMS 8
#define UFS_NATIVE_SG_MAX_BLOCKS (UFS_MAX_SIZE_PER_SG_SEGMENT / UFS_BLOCK_SIZE)
#define UFS_BIG_CLUSTER_CPU_AFFINITY_MASK 0xf0
// TODO: This parameter should be get from ufs device
#define DISCARD_MAX_BYTES 34359734272

#define UFS_MAX_BOOT_LUN 2

zx_status_t ufs_alloc_iobuf(mtk_ufs_t* ufs, ufs_iobuf_t* iob) {
    if (iob->align == 0) {
        iob->align = 1;
    }

    size_t size = iob->size + iob->align - 1;
    // bti_handle: IOMMU
    zx_status_t status = io_buffer_init(&iob->iob, ufs->bti_handle, size, iob->flags);
    if (status != ZX_OK) {
        UFS_LOGE("failed to alloc io buffer(%d)\n", status);
        return status;
    }

    iob->virt = io_buffer_virt(&iob->iob);
    iob->phys = io_buffer_phys(&iob->iob);

    iob->virt = (void*)ALIGN((uint64_t)iob->virt, iob->align);
    iob->phys = ALIGN(iob->phys, iob->align);

    return ZX_OK;
}

void ufs_free_iobuf(ufs_iobuf_t* iob) {
    if (!iob)
        return;
    if (io_buffer_is_valid(&iob->iob)) {
        io_buffer_release(&iob->iob);
        // clear all
        memset(iob, 0, sizeof(ufs_iobuf_t));
    }
}

void ufshcd_signal_event(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    /* Wakeup ufshcd_wait_command */
    zx_object_signal(hba->irq_event, 0u, UFS_IRQ_RECEIVED);
}

void ufs_dma_map(void* buf, size_t len, enum dma_data_direction dir)
{
    if (dir == DMA_FROM_DEVICE) {
        // Due to 'dc ivac' is inaccessible in EL0, use bidirectional instead.
        zx_cache_flush(buf, len, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    } else if (dir == DMA_TO_DEVICE) {
        /* write to device (or read from memory) */
        zx_cache_flush(buf, len, ZX_CACHE_FLUSH_DATA);
    } else {
        /* bi-directional or read from memory (or write to device) */
        zx_cache_flush(buf, len, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    }
}

void ufs_dma_unmap(void* buf, size_t len, enum dma_data_direction dir)
{
    if (!buf || !len)
        return;

    /* bi-directional or read from device (or write to memory) */
    if (dir != DMA_TO_DEVICE) {
        // NOTE:
        // Since 'dc ivac' is inaccessible in EL0, use bidirectional instead.
        // if CPU write this buf dirty during HW write, may encounter coherent
        // issue, by now, the case seems not happen.
        zx_cache_flush(buf, len, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    }
}

static void ufs_trans_reset_dma_state(ufs_trans_t* trans) {
    if (!trans) {
        return;
    }
    trans->pmt = ZX_HANDLE_INVALID;
    trans->sg_pmt_count = 0;
    for (uint32_t i = 0; i < BLOCK_OP_MAX_SG_REGIONS; i++) {
        trans->sg_pmts[i] = ZX_HANDLE_INVALID;
    }
    trans->compl_size = 0;
}

static void ufs_trans_unpin_dma(ufs_trans_t* trans) {
    if (!trans) {
        return;
    }
    if (trans->pmt != ZX_HANDLE_INVALID) {
        if (zx_pmt_unpin(trans->pmt) != ZX_OK) {
            UFS_LOGE("ufs: cannot unpin pmt?!\n");
        }
        trans->pmt = ZX_HANDLE_INVALID;
    }
    uint32_t sg_count = trans->sg_pmt_count;
    if (sg_count > BLOCK_OP_MAX_SG_REGIONS) {
        sg_count = BLOCK_OP_MAX_SG_REGIONS;
    }
    for (uint32_t i = 0; i < sg_count; i++) {
        if (trans->sg_pmts[i] != ZX_HANDLE_INVALID) {
            if (zx_pmt_unpin(trans->sg_pmts[i]) != ZX_OK) {
                UFS_LOGE("ufs: cannot unpin sg pmt?!\n");
            }
            trans->sg_pmts[i] = ZX_HANDLE_INVALID;
        }
    }
    trans->sg_pmt_count = 0;
}

static zx_status_t ufs_rw_cache_op(block_op_t* bop, uint32_t cache_flag,
                                   uint32_t blksz) {
    if (!bop) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (bop->rw.sg_count > 0) {
        if (bop->rw.sg_count > BLOCK_OP_MAX_SG_REGIONS) {
            return ZX_ERR_INVALID_ARGS;
        }
        for (uint32_t i = 0; i < bop->rw.sg_count; i++) {
            if (bop->rw.sg[i].length == 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            zx_status_t status = zx_vmo_op_range(
                bop->rw.vmo, cache_flag, bop->rw.sg[i].offset_vmo * blksz,
                bop->rw.sg[i].length * blksz, NULL, 0);
            if (status != ZX_OK) {
                return status;
            }
        }
        return ZX_OK;
    }
    return zx_vmo_op_range(bop->rw.vmo, cache_flag, bop->rw.offset_vmo * blksz,
                           bop->rw.length * blksz, NULL, 0);
}

static zx_status_t ufs_irq_thread(void* arg) {
    zx_status_t wait_ret;
    struct ufs_hba* hba = (struct ufs_hba*)arg;
#if PERF_BIND_CPU_TEMPORARILY
    uint32_t cpu_mask = UFS_BIG_CLUSTER_CPU_AFFINITY_MASK;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                (void*)&cpu_mask);
#endif
    zx_thread_set_priority(VBLOCK_DRV_THREAD_PRIORITY);

    while (true) {
        uint64_t slots;
        wait_ret = zx_interrupt_wait(hba->irq_handle, &slots);
        if (wait_ret != ZX_OK) {
            UFS_LOGE("ufs: ======zx_interrupt_wait failed=====>wait_ret:%x\n", wait_ret);
            break;
        }
        // handle irq
        //printf("mask %s %d\n", __func__, __LINE__);
        zx_interrupt_mask(hba->irq_handle, hba->irq);
        ufshcd_irq_handler(hba);
        zx_interrupt_unmask(hba->irq_handle, hba->irq);
       // printf("unmask %s %d\n", __func__, __LINE__);
    }
    return ZX_OK;
}

zx_status_t ufshcd_irq_init(struct ufs_hba *hba)
{
    /* Init event for thread function ufshcd_wait_command sleep/wakeup */
    zx_status_t status;

    status = zx_event_create(0u, &hba->irq_event);
    if (status != ZX_OK) {
        UFS_LOGE("ufs: failed to create irq event\n");
        return status;
    }

    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);

    status = pdev_map_interrupt(&ufs->pdev, 0, &hba->irq_handle);
    if (status != ZX_OK) {
        UFS_LOGE("ufs: pdev_map_interrupt failed\n");
        zx_handle_close(hba->irq_event);
        return status;
    }

    pdev_get_irq_info(&ufs->pdev, 0, &hba->out_irq);
    hba->irq = hba->out_irq.irq;
    UFS_LOGI("irq %u\n", hba->irq);

    zx_interrupt_bind_pcpu(hba->irq_handle, hba->irq, 4);

    // Mask interrupt
   // pdev_interrupt_mask(&ufs->pdev, 0, hba->irq_handle);
   // ufs_mask_interrupt(hba, hba->irq);
    //printf("mask %s %d\n", __func__, __LINE__);
    zx_interrupt_mask(hba->irq_handle, hba->irq);
    status = thrd_create_with_name(&hba->irq_thread, ufs_irq_thread, hba,
                                   "ufs-irq");
    if (status != thrd_success) {
        UFS_LOGE("ufs: failed to create irq thread\n");
        zx_handle_close(hba->irq_handle);
        zx_handle_close(hba->irq_event);
        return status;
    }
    thrd_detach(hba->irq_thread);
    zx_interrupt_unmask(hba->irq_handle, hba->irq);

    return ZX_OK;
}

void ufshcd_complete_bop(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    block_op_t *bop = lrbp->bop;
    ufs_trans_t *trans = to_ufs_trans(bop);
    zx_status_t status = ZX_OK;

    if (bop->command == BLOCK_OP_FLUSH ||
        bop->command == BLOCK_OP_DISCARD ||
        bop->command == BLOCK_OP_SECURE_ERASE ||
        bop->command == BLOCK_OP_WRITE_ZEROES) {
        UFS_LOGD("cmd: %u tag: %u, exp_len: %lu \n", bop->command, lrbp->task_tag, lrbp->exp_len);
        bop->completion_cb(bop, status);
        if (lrbp->iob_unmap) {
            ufs_free_iobuf((ufs_iobuf_t*)lrbp->iob_unmap);
            free(lrbp->iob_unmap);
            lrbp->iob_unmap = NULL;
        }
        return;
    }

    if (bop->command == BLOCK_OP_READ || bop->command == BLOCK_OP_WRITE) {
        trans->compl_size += lrbp->exp_len;
        UFS_LOGD("cmd: %u tag: %u, compl_size: 0x%x, expected: 0x%x\n", bop->command,
                 lrbp->task_tag, trans->compl_size, bop->rw.length * UFS_BLOCK_SIZE);
        if (trans->compl_size == bop->rw.length * UFS_BLOCK_SIZE) {
            if (bop->command == BLOCK_OP_READ && !bop->rw.cache_flushed) {
                status = ufs_rw_cache_op(bop, ZX_VMO_OP_CACHE_INVALIDATE,
                                         UFS_BLOCK_SIZE);
                if (status != ZX_OK)
                    UFS_LOGE("ufs: cache op failed: %d\n", status);
            }

            ufs_trans_unpin_dma(trans);
            bop->completion_cb(bop, status);
        }
    }
}

static bool ufs_eh_in_progress(struct ufs_hba *hba)
{
    bool in_progress;

    mutex_lock(&hba->free_tag_lock);
    in_progress = ufshcd_eh_in_progress(hba);
    mutex_unlock(&hba->free_tag_lock);

    return in_progress;
}

void ufs_wake_worker_thread(struct ufs_hba *hba)
{
    mtk_ufs_t* ufs = hba_to_ufs(hba);
    if (ufs->worker_event != ZX_HANDLE_INVALID) {
        zx_object_signal(ufs->worker_event, 0, UFS_TRANS_RECEIVED | UFS_EH_DONE);
    }
}

static zx_status_t ufs_block_rw_single(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev,
                                       block_op_t* bop, uint32_t blksz) {
    zx_status_t status;
    zx_paddr_t phys;
    ufs_trans_t* trans = to_ufs_trans(bop);
    zx_handle_t pmt = ZX_HANDLE_INVALID;
    bool is_read = bop->command == BLOCK_OP_READ;

    ufs_trans_reset_dma_state(trans);
    status = zx_bti_pin_new(ufs->bti_handle, ZX_BTI_PERM_READ,
                            bop->rw.vmo, bop->rw.offset_vmo * blksz,
                            blksz, &phys, 1, &pmt);
    if (status != ZX_OK) {
        UFS_LOGE("ufs: bti_pin failed, can't pin vmo: %d\n", status);
        goto out;
    }
    trans->pmt = pmt;

    if (!bop->rw.cache_flushed) {
        uint32_t cache_flag = is_read ? ZX_VMO_OP_CACHE_INVALIDATE :
                                        ZX_VMO_OP_CACHE_CLEAN;
        status = ufs_rw_cache_op(bop, cache_flag, blksz);
        if (status != ZX_OK) {
            UFS_LOGE("ufs: cache op failed: %d\n", status);
            goto out;
        }
    }

    struct ufs_hba* hba = &ufs->hba;
    uint32_t blk_start = bop->rw.offset_dev;

    trans->compl_size = 0;

    UFS_IO_DBG("bop start %u, count 1\n", blk_start);
    if (is_read) {
        status = hba->blk_read(hba, bop, lu_dev->lun, blk_start, 1, phys);
    } else {
        status = hba->blk_write(hba, bop, lu_dev->lun, blk_start, 1, phys);
    }
    if (status != ZX_OK) {
        UFS_LOGE("ufs: %s fail status %d\n", is_read ? "read" : "write", status);
        dump_bop(bop);
        goto out;
    }
    return status;

out:
    ufs_trans_unpin_dma(trans);

    return status;
}

static zx_status_t __attribute__((noinline))
ufs_block_rw_multi(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev, block_op_t* bop,
                   uint32_t blksz) {
    uint32_t total_blk_cnt = bop->rw.length;
    zx_status_t status = ZX_OK;
    zx_paddr_t phys_list[VBLOCK_DRV_ALLOC];
    ufs_trans_t* trans = to_ufs_trans(bop);

    zx_handle_t pmt = ZX_HANDLE_INVALID;
    // Must do bti_pin to commit real physcial pages prior to any cache
    // operations.
    ufs_trans_reset_dma_state(trans);
    status = zx_bti_pin_new(ufs->bti_handle, ZX_BTI_PERM_READ,
                            bop->rw.vmo, bop->rw.offset_vmo * blksz,
                            total_blk_cnt * blksz, phys_list, total_blk_cnt, &pmt);
    if (status != ZX_OK) {
        UFS_LOGE("ufs: bti_pin failed, can't pin vmo: %d\n", status);
        goto out;
    }
    trans->pmt = pmt;

    bool is_read = bop->command == BLOCK_OP_READ;
    if (!bop->rw.cache_flushed) {
        // For UFS, no bi-directional case to be considered.
        uint32_t cache_flag = is_read ? ZX_VMO_OP_CACHE_INVALIDATE :
                                        ZX_VMO_OP_CACHE_CLEAN;
        status = ufs_rw_cache_op(bop, cache_flag, blksz);
        if (status != ZX_OK) {
            UFS_LOGE("ufs: cache op failed: %d\n", status);
            goto out;
        }
    }

    struct ufs_hba* hba = &ufs->hba;
    trans->compl_size = 0;

    uint32_t blk_start = bop->rw.offset_dev;

    UFS_IO_DBG("bop start %u, count %u\n", blk_start, total_blk_cnt);

    phys_iter_buffer_t pbuf = {
        .phys = phys_list,
        .phys_count = total_blk_cnt,
        .length = total_blk_cnt * blksz,
        .vmo_offset = bop->rw.offset_vmo * blksz,
    };
    phys_iter_t piter;
    phys_iter_init(&piter, &pbuf, total_blk_cnt * blksz);

    uint32_t blk_cnt_c = 0;

    for (;;) {
        zx_paddr_t pa;
        size_t length = phys_iter_next(&piter, &pa);
        uint32_t blk_cnt = length / blksz;

        // trans complete
        if (blk_cnt_c >= total_blk_cnt)
            break;

        if (length == 0)
            break;

        if (length < 0 || length > total_blk_cnt * blksz) {
            UFS_LOGE("ufs: chunk size is unsupported %zu\n", length);
            status = ZX_ERR_NOT_SUPPORTED;
            goto out;
        }

        if (is_read) {
            status = hba->blk_read(hba, bop, lu_dev->lun, blk_start, blk_cnt, pa);
        } else {
            status = hba->blk_write(hba, bop, lu_dev->lun, blk_start, blk_cnt, pa);
        }
        if (status != ZX_OK) {
            UFS_LOGE("ufs: %s fail status %d\n", is_read ? "read" : "write", status);
            dump_bop(bop);
            goto out;
        }
        blk_start += blk_cnt;
        blk_cnt_c += blk_cnt;
    }

    UFS_IO_DBG("block rw queue done\n");

    return status;

out:
    ufs_trans_unpin_dma(trans);

    return status;
}

static zx_status_t __attribute__((noinline))
ufs_block_rw_native_sg(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev,
                       block_op_t* bop, uint32_t blksz) {
    uint32_t total_blk_cnt = bop->rw.length;
    uint32_t sg_count = bop->rw.sg_count;
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    zx_paddr_t phys_list[UFS_NATIVE_SG_MAX_BLOCKS];
    struct ufs_dma_sg_entry dma_sg[UFS_NATIVE_SG_MAX_BLOCKS];
    uint32_t phys_index = 0;
    uint32_t dma_count = 0;
    uint32_t blocks_seen = 0;
    ufs_trans_t* trans = to_ufs_trans(bop);
    bool is_read = bop->command == BLOCK_OP_READ;

    if ((!is_read && bop->command != BLOCK_OP_WRITE) || sg_count <= 1 ||
        sg_count > BLOCK_OP_MAX_SG_REGIONS || total_blk_cnt == 0 ||
        total_blk_cnt > UFS_NATIVE_SG_MAX_BLOCKS) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ufs_trans_reset_dma_state(trans);

    for (uint32_t i = 0; i < sg_count; i++) {
        uint32_t seg_blocks = bop->rw.sg[i].length;
        uint32_t seg_bytes = seg_blocks * blksz;
        zx_handle_t pmt = ZX_HANDLE_INVALID;

        if (seg_blocks == 0 || seg_blocks > total_blk_cnt - blocks_seen ||
            phys_index > UFS_NATIVE_SG_MAX_BLOCKS - seg_blocks) {
            status = ZX_ERR_INVALID_ARGS;
            goto out;
        }

        status = zx_bti_pin_new(ufs->bti_handle, ZX_BTI_PERM_READ,
                                bop->rw.vmo, bop->rw.sg[i].offset_vmo * blksz,
                                seg_bytes, &phys_list[phys_index], seg_blocks,
                                &pmt);
        if (status != ZX_OK) {
            UFS_LOGE("ufs: sg bti_pin failed, can't pin vmo: %d\n", status);
            goto out;
        }

        trans->sg_pmts[trans->sg_pmt_count++] = pmt;

        phys_iter_buffer_t pbuf = {
            .phys = &phys_list[phys_index],
            .phys_count = seg_blocks,
            .length = seg_bytes,
            .vmo_offset = bop->rw.sg[i].offset_vmo * blksz,
        };
        phys_iter_t piter;
        phys_iter_init(&piter, &pbuf, seg_bytes);

        uint32_t consumed = 0;
        while (consumed < seg_bytes) {
            zx_paddr_t pa;
            size_t length = phys_iter_next(&piter, &pa);
            if (length == 0 || length > seg_bytes - consumed ||
                (length % blksz) != 0 ||
                dma_count >= UFS_NATIVE_SG_MAX_BLOCKS) {
                status = ZX_ERR_NOT_SUPPORTED;
                goto out;
            }
            dma_sg[dma_count].paddr = pa;
            dma_sg[dma_count].len = (uint32_t)length;
            dma_count++;
            consumed += (uint32_t)length;
        }

        phys_index += seg_blocks;
        blocks_seen += seg_blocks;
    }

    if (blocks_seen != total_blk_cnt || dma_count == 0) {
        status = ZX_ERR_INVALID_ARGS;
        goto out;
    }

    if (!bop->rw.cache_flushed) {
        status = ufs_rw_cache_op(bop, is_read ? ZX_VMO_OP_CACHE_INVALIDATE
                                               : ZX_VMO_OP_CACHE_CLEAN,
                                 blksz);
        if (status != ZX_OK) {
            UFS_LOGE("ufs: sg cache op failed: %d\n", status);
            goto out;
        }
    }

    trans->compl_size = 0;

    UFS_IO_DBG("bop native sg %s start %llu, count %u, regions %u, chunks %u\n",
               is_read ? "read" : "write",
               (unsigned long long)bop->rw.offset_dev, total_blk_cnt,
               sg_count, dma_count);

    if (is_read) {
        status = ufs_dma_read_sg(&ufs->hba, bop, lu_dev->lun,
                                 bop->rw.offset_dev, total_blk_cnt, dma_sg,
                                 dma_count);
    } else {
        status = ufs_dma_write_sg(&ufs->hba, bop, lu_dev->lun,
                                  bop->rw.offset_dev, total_blk_cnt, dma_sg,
                                  dma_count);
    }
    if (status != ZX_OK) {
        UFS_LOGE("ufs: %s_sg fail status %d\n", is_read ? "read" : "write",
                 status);
        dump_bop(bop);
        goto out;
    }

    return ZX_OK;

out:
    ufs_trans_unpin_dma(trans);
    return status;
}

zx_status_t ufs_block_rw(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev, block_op_t* bop) {
    // TODO:
    // 1. MTK use fixed block_size(4KiB), which is equal to PAGE_SIZE in zircon,
    // so here use block_size to split the buffer to simplify the implement of
    // block data I/O.
    // Once one of block_size and PAGE_SIZE changed, need to restructure the logic of
    // block r/w loop.
    ZX_ASSERT(lu_dev->block_info.block_size == PAGE_SIZE);
    // TODO:
    // 2. block layer defines the maximum transfer size per IO(MAX_XFER=8KiB),
    // which is less than limit_blk_cnt(MTK's UFS controller limitation), so
    // here we just do sanity check here.
    uint32_t blksz = lu_dev->block_info.block_size;
    u32 limit_blk_cnt = UFS_MAX_DATA_SIZE_PER_CMD_MB * 1024 * 1024 / blksz;
    ZX_ASSERT(bop->rw.length <= limit_blk_cnt);

    if (bop->rw.sg_count > 0) {
        return ufs_block_rw_native_sg(ufs, lu_dev, bop, blksz);
    }

    if (bop->rw.length == 1) {
        return ufs_block_rw_single(ufs, lu_dev, bop, blksz);
    }

    return ufs_block_rw_multi(ufs, lu_dev, bop, blksz);
}

zx_status_t ufs_block_flush(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev, block_op_t* bop) {
    struct ufs_hba* hba = &ufs->hba;
    //flush all LU
    return hba->blk_flush(hba, bop, lu_dev->lun, 0, 0);
    //return hba->blk_flush(hba, bop, lu_dev->lun, 0, hba->blk_cnt[lu_dev->lun]);
}

zx_status_t ufs_block_erase(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev, block_op_t* bop) {
    struct ufs_hba* hba = &ufs->hba;
    return hba->blk_erase(hba, bop, lu_dev->lun, bop->erase.offset_dev, bop->erase.length);
}

zx_status_t ufs_block_secure_erase(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev, block_op_t* bop) {
    struct ufs_hba* hba = &ufs->hba;
    return hba->blk_secure_erase(hba, bop, lu_dev->lun, bop->erase.offset_dev, bop->erase.length);
}

zx_status_t ufs_block_write_zeroes(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev, block_op_t* bop) {
    struct ufs_hba* hba = &ufs->hba;
    return hba->blk_write_zeroes(hba, bop, lu_dev->lun, bop->erase.offset_dev, bop->erase.length);
}

// Nothing can do if failed to allocate, just assert
static void ufs_dma_buf_init(mtk_ufs_t* ufs) {
    struct ufs_hba* hba = &ufs->hba;
    ufs_iobuf_t* iob;
    zx_status_t status;

    // Allocate memory for UTP command descriptors
    iob = &ufs->ucdl;
    iob->size = sizeof(struct utp_transfer_cmd_desc) * UFS_MAX_NUTRS;
    iob->align = UFS_ALIGN_UCD(ufs);
    //iob->flags = IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED;
    iob->flags = IO_BUFFER_RW | IO_BUFFER_CONTIG;
    status = ufs_alloc_iobuf(ufs, iob);
    ZX_ASSERT(status == ZX_OK);
    hba->ucdl_base_addr = iob->virt;
    hba->ucdl_dma_addr = iob->phys;

    // Allocate memory for UTP Transfer descriptors
    // UFSHCI requires 1024 byte alignment of UTRD
    iob = &ufs->utrdl;
    iob->size = sizeof(struct utp_transfer_req_desc) * UTRD_NUTRS(ufs);
    iob->align = UFS_ALIGN_UTRD(ufs);
    iob->flags = IO_BUFFER_RW | IO_BUFFER_CONTIG |IO_BUFFER_UNCACHED;
    status = ufs_alloc_iobuf(ufs, iob);
    ZX_ASSERT(status == ZX_OK);
    hba->utrdl_base_addr = iob->virt;
    hba->utrdl_dma_addr = iob->phys;
}

static void ufs_dma_buf_free(mtk_ufs_t* ufs) {
    ufs_free_iobuf(&ufs->ucdl);
    ufs_free_iobuf(&ufs->utrdl);
}

static void ufs_do_trans(mtk_ufs_t* ufs, ufs_trans_t* trans) {
    zx_status_t status;
    ufs_lu_dev_t* lu_dev = trans->lu_dev;

    UFS_LOGD("ufs: do_trans blockop 0x%x offset_vmo 0x%" PRIx64 " length 0x%x blocksize 0x%x"
             " max_transfer_size 0x%x\n",
             trans->bop.command, trans->bop.rw.offset_vmo, trans->bop.rw.length,
             lu_dev->block_info.block_size, lu_dev->block_info.max_transfer_size);

    switch (trans->bop.command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        status = ufs_block_rw(ufs, lu_dev, &trans->bop);
        if (status != ZX_OK) {
            UFS_LOGE("ufs: %s error %d\n", block_op_str((uint8_t)trans->bop.command &
				    BLOCK_OP_MASK), status);
            trans->bop.completion_cb(&trans->bop, status);
        }
        break;
    case BLOCK_OP_FLUSH:
        status = ufs_block_flush(ufs, lu_dev, &trans->bop);
        break;
    case BLOCK_OP_DISCARD:
        status = ufs_block_erase(ufs, lu_dev, &trans->bop);
        break;
    case BLOCK_OP_SECURE_ERASE:
        status = ufs_block_secure_erase(ufs, lu_dev, &trans->bop);
        break;
    case BLOCK_OP_WRITE_ZEROES:
        status = ufs_block_write_zeroes(ufs, lu_dev, &trans->bop);
        break;
    default:
        // should not get here
        UFS_LOGE("ufs: do_trans invalid block op %d\n",
                 trans->bop.command & BLOCK_OP_MASK);
        status = ZX_ERR_INVALID_ARGS;
        trans->bop.completion_cb(&trans->bop, status);
        ZX_DEBUG_ASSERT(true);
        break;
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "ufs: %s error %d\n", block_op_str((uint8_t)trans->bop.command &
               BLOCK_OP_MASK), status);
    }
}

static bool ufs_is_direct_submit_candidate(block_op_t* bop) {
    if (!bop) {
        return false;
    }

    uint32_t command = bop->command & BLOCK_OP_MASK;

    if ((command == BLOCK_OP_READ || command == BLOCK_OP_WRITE) &&
        bop->rw.length == 1) {
        return true;
    }

    bool is_native_sg_rw = (command == BLOCK_OP_READ ||
        command == BLOCK_OP_WRITE) &&
        bop->rw.sg_count > 0 &&
        bop->rw.sg_count <= BLOCK_OP_MAX_SG_REGIONS &&
        bop->rw.length > 0 &&
        bop->rw.length <= UFS_NATIVE_SG_MAX_BLOCKS;

    return is_native_sg_rw;
}

static int ufs_worker_thread(void* arg) {
    mtk_ufs_t* ufs = (mtk_ufs_t*)arg;
    uint32_t pending;
    zx_status_t status;
    ufs_pm_state_t state;
#if PERF_BIND_CPU_TEMPORARILY
    uint32_t cpu_mask = UFS_BIG_CLUSTER_CPU_AFFINITY_MASK;
    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                (void*)&cpu_mask);
#endif
    zx_thread_set_priority(VBLOCK_DRV_THREAD_PRIORITY);

    for (uint32_t i = 0; i < UFS_LU_INTERNAL_CNT; i++) {
        if (ufs->lu_devs[i] && ufs->lu_devs[i]->zxdev) {
            device_make_visible(ufs->lu_devs[i]->zxdev);
        }
    }
    for (;;) {
        mtx_lock(&ufs->lock);
        state = ufs->state;
        mtx_unlock(&ufs->lock);

        if (state != UFS_PM_ACTIVE) {
            status = zx_object_wait_one(ufs->worker_event,
                                        UFS_STATE_CHANGED | UFS_EH_DONE | UFS_SHUTDOWN,
                                        ZX_TIME_INFINITE, &pending);
            if (status != ZX_OK) {
                UFS_LOGE("ufs: worker thread wait failed, retcode = %d\n", status);
                break;
            }
            if (pending & UFS_SHUTDOWN) {
                zx_object_signal(ufs->worker_event, pending, UFS_SHUTDOWN_DONE);
                break;
            }
            if (pending & UFS_EH_DONE) {
                zx_object_signal(ufs->worker_event, UFS_EH_DONE, 0);
            }
            if (pending & UFS_STATE_CHANGED) {
                zx_object_signal(ufs->worker_event, UFS_STATE_CHANGED, 0);
            }
            continue;
        }

        if (ufs_eh_in_progress(&ufs->hba)) {
            status = zx_object_wait_one(ufs->worker_event,
                                        UFS_EH_DONE | UFS_SHUTDOWN,
                                        ZX_TIME_INFINITE, &pending);
            if (status != ZX_OK) {
                UFS_LOGE("ufs: worker thread wait failed, retcode = %d\n", status);
                break;
            }
            if (pending & UFS_SHUTDOWN) {
                zx_object_signal(ufs->worker_event, pending, UFS_SHUTDOWN_DONE);
                break;
            }
            if (pending & UFS_EH_DONE) {
                zx_object_signal(ufs->worker_event, UFS_EH_DONE, 0);
            }
            continue;
        }

        // Move queued transactions to a local list to shorten the time spent
        // holding ufs->lock, but stop draining promptly once EH starts.
        mtx_lock(&ufs->lock);
        list_node_t local_list = {
            .next = &local_list,
            .prev = &local_list
        };

        if (!list_is_empty(&ufs->trans_list)) {
            list_node_t *first = ufs->trans_list.next;
            list_node_t *last  = ufs->trans_list.prev;

            ufs->trans_list.next = &ufs->trans_list;
            ufs->trans_list.prev = &ufs->trans_list;

            local_list.next = first;
            local_list.prev = last;
            first->prev = &local_list;
            last->next  = &local_list;
        }

        // Clear the received signal while holding the queue lock so new
        // arrivals cannot race with the worker going back to sleep.
        zx_object_signal(ufs->worker_event, UFS_TRANS_RECEIVED, 0);
        mtx_unlock(&ufs->lock);

        bool deferred_to_eh = false;
        ufs_trans_t *trans;
        while ((trans = list_remove_head_type(&local_list, ufs_trans_t, node)) != NULL) {
            if (ufs_eh_in_progress(&ufs->hba)) {
                mtx_lock(&ufs->lock);
                list_add_tail(&ufs->trans_list, &trans->node);
                while ((trans = list_remove_head_type(&local_list, ufs_trans_t, node)) != NULL) {
                    list_add_tail(&ufs->trans_list, &trans->node);
                }
                mtx_unlock(&ufs->lock);
                deferred_to_eh = true;
                break;
            }

            ufs_do_trans(ufs, trans);
        }

        if (deferred_to_eh) {
            continue;
        }

        mtx_lock(&ufs->lock);
        ufs->worker_waiting = true;
        mtx_unlock(&ufs->lock);

        status = zx_object_wait_one(ufs->worker_event,
                                UFS_TRANS_RECEIVED | UFS_SHUTDOWN,
                                ZX_TIME_INFINITE, &pending);

        mtx_lock(&ufs->lock);
        ufs->worker_waiting = false;
        mtx_unlock(&ufs->lock);
        if (status != ZX_OK) {
            UFS_LOGE("ufs: worker thread wait failed, retcode = %d\n", status);
            break;
        }
        if (pending & UFS_SHUTDOWN) {
            zx_object_signal(ufs->worker_event, pending, UFS_SHUTDOWN_DONE);
            break;
        }
    }

    UFS_LOGW("ufs: worker thread terminated\n");

    return 0;
}

// block layer interface
static void ufs_query(void* ctx, block_info_t* info_out,
                      size_t* block_op_size_out) {
    ufs_lu_dev_t* lu_dev = ctx;

    memcpy(info_out, &lu_dev->block_info, sizeof(*info_out));
    *block_op_size_out = sizeof(ufs_trans_t);
}

static void ufs_queue(void* ctx, block_op_t* bop) {
    ufs_lu_dev_t* lu_dev = ctx;
    mtk_ufs_t* ufs = lu_dev->ufs;
    ufs_trans_t* trans = containerof(bop, ufs_trans_t, bop);
    uint64_t max = lu_dev->block_info.block_count;

    trans->lu_dev = lu_dev;

    switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        if ((bop->rw.offset_dev >= max) ||
            ((max - bop->rw.offset_dev) < bop->rw.length)) {
            bop->completion_cb(bop, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        if (bop->rw.length == 0) {
            bop->completion_cb(bop, ZX_OK);
            return;
        }
        break;
    }
    case BLOCK_OP_FLUSH:
        // queue the flush op. because there is no out of order execution in this
        // driver, when this op gets processed all previous ops are complete.
        break;
    case BLOCK_OP_DISCARD:
    case BLOCK_OP_WRITE_ZEROES:
    case BLOCK_OP_SECURE_ERASE: {
        if ((bop->erase.offset_dev >= max) ||
            ((max - bop->erase.offset_dev) < bop->erase.length)) {
            bop->completion_cb(bop, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        if (bop->erase.length == 0) {
            bop->completion_cb(bop, ZX_OK);
            return;
        }
        break;
    }
    default:
        bop->completion_cb(bop, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    bool direct_submit = false;
    bool direct_submit_active = false;
    bool eh_in_progress = ufs_eh_in_progress(&ufs->hba);
    mtx_lock(&ufs->lock);
    bool was_empty = list_is_empty(&ufs->trans_list);
    direct_submit_active = ufs->direct_submit_active;
    if (was_empty && ufs->worker_waiting && !ufs->direct_submit_active &&
        !eh_in_progress && ufs_is_direct_submit_candidate(bop)) {
        ufs->direct_submit_active = true;
        direct_submit_active = true;
        direct_submit = true;
    }
    ufs_trans_reset_dma_state(trans);
    if (!direct_submit) {
        list_add_tail(&ufs->trans_list, &trans->node);
    }
    mtx_unlock(&ufs->lock);

    if (direct_submit) {
        ufs_do_trans(ufs, trans);
        mtx_lock(&ufs->lock);
        ufs->direct_submit_active = false;
        bool has_pending = !list_is_empty(&ufs->trans_list);
        mtx_unlock(&ufs->lock);
        if (has_pending) {
            zx_object_signal(ufs->worker_event, 0, UFS_TRANS_RECEIVED);
        }
        return;
    }

    if (was_empty && !direct_submit_active) {
        zx_object_signal(ufs->worker_event, 0, UFS_TRANS_RECEIVED);
    }
}

// Block protocol
static block_protocol_ops_t block_proto = {
    .query = ufs_query,
    .queue = ufs_queue,
};

static zx_status_t ufs_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    ufs_lu_dev_t* lu_dev = ctx;
    mtk_ufs_t* ufs = lu_dev->ufs;
    int ret;

    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = out_buf;
        if (out_len < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(info, &lu_dev->block_info, sizeof(*info));
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_VBLOCK_SET_WRITE_PROTECT: {
        return ZX_OK;
    }
    case IOCTL_VBLOCK_SET_BOOT_REGION: {
        u32 lu;

        if (!in_buf || in_len < sizeof(lu))
            return ZX_ERR_INVALID_ARGS;

        lu = *(const u32*)in_buf;
        if (lu == 0 || lu > UFS_MAX_BOOT_LUN)
            return ZX_ERR_OUT_OF_RANGE;
        mtx_lock(&ufs->lock);
        ret = ufs_set_boot_lu(&ufs->hba, lu);
        mtx_unlock(&ufs->lock);
        if (ret)
            return ZX_ERR_INTERNAL;

        return ZX_OK;
    }
    case IOCTL_VBLOCK_GET_BOOTDEV_TYPE: {
        return ZX_OK;
    }
    case IOCTL_VBLOCK_GET_ACTIVE_BOOT: {
        u32* active_boot_part = out_buf;

        if (!out_buf)
            return ZX_ERR_INVALID_ARGS;

        if (out_len < sizeof(*active_boot_part))
            return ZX_ERR_BUFFER_TOO_SMALL;

        mtx_lock(&ufs->lock);
        ret = ufs_get_active_boot_part(active_boot_part);
        mtx_unlock(&ufs->lock);
        if (ret)
            return ZX_ERR_INTERNAL;

        if (out_actual)
            *out_actual = sizeof(*active_boot_part);
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t ufs_vrpmb_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len, size_t* out_actual) {
    mtk_ufs_t* ufs = ctx;

    switch (op) {
    case GRT_VRPMB_IOCTL_START:
    case GRT_VRPMB_IOCTL_GET_HANDLE:
    case GRT_VRPMB_IOCTL_STOP:
        return ufs_virtio_rpmb_ioctl(ufs, op, in_buf, in_len, out_buf, out_len, out_actual);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_off_t ufs_get_size(void* ctx) {
    ufs_lu_dev_t* lu_dev = ctx;

    return lu_dev->block_info.block_size * lu_dev->block_info.block_count;
}

static void ufs_unbind(void* ctx) {
    ufs_lu_dev_t* lu_dev = ctx;
    mtk_ufs_t* ufs = lu_dev->ufs;

    mtx_lock(&ufs->lock);
    ufs->state = UFS_PM_POWEROFF;
    mtx_unlock(&ufs->lock);

    // do not deinit ufs, let following system do reinit
    //ufs_deinit(ufs);

    device_remove(lu_dev->zxdev);
}

static void ufs_release(void* ctx) {
    ufs_lu_dev_t* lu_dev = ctx;
    mtk_ufs_t* ufs = lu_dev->ufs;
    bool release_host = false;

    mtx_lock(&ufs->lock);
    if (lu_dev->lun < UFS_LU_INTERNAL_CNT) {
        lu_dev->zxdev = NULL;
    }
    ufs->lu_release_count++;
    release_host = (ufs->lu_dev_count > 0 && ufs->lu_release_count >= ufs->lu_dev_count);
    mtx_unlock(&ufs->lock);

    if (!release_host) {
        return;
    }

    ufshcd_eh_exit(&ufs->hba);

    if (ufs->worker_thread_running) {
        mtx_lock(&ufs->lock);
        ufs->state = UFS_PM_POWEROFF;
        mtx_unlock(&ufs->lock);

        zx_object_signal(ufs->worker_event, 0, UFS_STATE_CHANGED);
        // signal the worker thread and wait for it to terminate
        zx_object_signal(ufs->worker_event, 0, UFS_SHUTDOWN);
        zx_object_wait_one(ufs->worker_event, UFS_SHUTDOWN_DONE, ZX_TIME_INFINITE, NULL);

        // error out all pending requests
        ufs_trans_t* trans = NULL;
        list_for_every_entry(&ufs->trans_list, trans, ufs_trans_t, node) {
            trans->bop.completion_cb(&trans->bop, ZX_ERR_BAD_STATE);
        }

        thrd_join(ufs->worker_thread, NULL);
    }

    if (ufs->worker_event != ZX_HANDLE_INVALID) {
        zx_handle_close(ufs->worker_event);
    }

    ufs_virtio_rpmb_release(ufs);
    ufs_dma_buf_free(ufs);
    for (uint32_t i = 0; i < UFS_LU_INTERNAL_CNT; i++) {
        free(ufs->lu_devs[i]);
        ufs->lu_devs[i] = NULL;
    }
    mtx_destroy(&ufs->lock);

    free(ufs);
}

static void ufs_vrpmb_unbind(void* ctx) {
    mtk_ufs_t* ufs = ctx;

    if (ufs->vrpmb_zxdev) {
        device_remove(ufs->vrpmb_zxdev);
    }
}

static void ufs_vrpmb_release(void* ctx) {
    mtk_ufs_t* ufs = ctx;

    ufs->vrpmb_zxdev = NULL;
}

static zx_status_t ufs_vrpmb_suspend(void* ctx, uint32_t flags) {
    mtk_ufs_t* ufs = ctx;

    UFS_LOGI("rpmb suspend flags=0x%x\n", flags);
    return ufs_virtio_rpmb_suspend(ufs, flags);
}

static zx_status_t ufs_vrpmb_resume(void* ctx, uint32_t flags) {
    mtk_ufs_t* ufs = ctx;

    UFS_LOGI("rpmb resume flags=0x%x\n", flags);
    return ufs_virtio_rpmb_resume(ufs, flags);
}

static zx_status_t ufs_dev_suspend(void* ctx, uint32_t flags) {
    ufs_lu_dev_t* lu_dev = ctx;
    mtk_ufs_t* ufs = lu_dev->ufs;
    int ret;
    bool need_signal = false;
    zx_status_t status = ZX_OK;

    UFS_LOGI("start suspend flags=0x%x lun:%d\n", flags, lu_dev->lun);

    mtx_lock(&ufs->lock);
    if (ufs->state == UFS_PM_SUSPEND) {
        UFS_LOGD("already suspended\n");
    } else {
        ret = ufs_suspend(&ufs->hba);
        if (ret) {
            UFS_LOGE("suspend failed ret=%d\n", ret);
            status = ZX_ERR_INTERNAL;
        } else {
            ufs->state = UFS_PM_SUSPEND;
            need_signal = true;
        }
    }
    mtx_unlock(&ufs->lock);

    if (need_signal)
        zx_object_signal(ufs->worker_event, 0, UFS_STATE_CHANGED);

    UFS_LOGI("suspend done status:%d lun:%d\n", status, lu_dev->lun);
    return status;
}

static zx_status_t ufs_dev_resume(void* ctx, uint32_t flags) {
    ufs_lu_dev_t* lu_dev = ctx;
    mtk_ufs_t* ufs = lu_dev->ufs;
    int ret;
    bool need_signal = false;
    zx_status_t status = ZX_OK;

    UFS_LOGI("resume start flags=0x%x lun:%d\n", flags, lu_dev->lun);

    mtx_lock(&ufs->lock);
    if (ufs->state == UFS_PM_ACTIVE) {
        UFS_LOGI("already active\n");
    } else {
        ret = ufs_resume(&ufs->hba);
        if (ret) {
            UFS_LOGE("resume failed ret=%d\n", ret);
            status = ZX_ERR_INTERNAL;
        } else {
            ufs->state = UFS_PM_ACTIVE;
            need_signal = true;
        }
    }
    mtx_unlock(&ufs->lock);

    if (need_signal)
        zx_object_signal(ufs->worker_event, 0, UFS_STATE_CHANGED);

    UFS_LOGI("resume done status:%d lun:%d\n", status, lu_dev->lun);
    return status;
}

static zx_protocol_device_t ufs_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ufs_ioctl,
    .get_size = ufs_get_size,
    .unbind = ufs_unbind,
    .release = ufs_release,
    .suspend = ufs_dev_suspend,
    .resume = ufs_dev_resume,
};

static zx_protocol_device_t ufs_vrpmb_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ufs_vrpmb_ioctl,
    .unbind = ufs_vrpmb_unbind,
    .release = ufs_vrpmb_release,
    .suspend = ufs_vrpmb_suspend,
    .resume = ufs_vrpmb_resume,
};

static zx_status_t ufs_protocol_get_info(void* ctx, zx_handle_t* handle_out) {
    return ZX_OK;
}

// For further used:
// board driver can use ufs via this protocol
static ufs_protocol_ops_t ufs_protocol_ops = {
    .get_info = ufs_protocol_get_info,
};

static void ufs_init_lu_block_info(mtk_ufs_t* ufs, ufs_lu_dev_t* lu_dev) {
    lu_dev->block_info.block_size = UFS_BLOCK_SIZE;
    lu_dev->block_info.block_count = ufs->hba.blk_cnt[lu_dev->lun];
    lu_dev->block_info.block_id = lu_dev->lun;
    lu_dev->block_info.flags |= BLOCK_FLAG_BLOCK_ID_VALID;
    lu_dev->block_info.max_transfer_size = UFS_MAX_DATA_SIZE_PER_CMD_MB * 1024 * 1024;
    lu_dev->block_info.max_seg_nums = UFS_BLOCK_MAX_SEG_NUMS;

    // TODO: init ufs->dev_profile

    // TODO: init lu_dev->block_info.crypto_cap

    // init block_proto->crypto_profile
    // done in ufshcd_init

    // TODO: init lu_dev->block_info.crypto_cap
    // just copy
#ifdef UFS_HW_CRYPT_EN
    lu_dev->block_info.crypto_cap = ufs->hba.crypto_profile_dev.caps;
    lu_dev->block_info.inline_crypto_supported = 0x1;
#else
    lu_dev->block_info.inline_crypto_supported = 0x0;
#endif

    lu_dev->block_info.wce = 1;    //enable write cache - writeback
    lu_dev->block_info.discard_max_bytes = DISCARD_MAX_BYTES;
    lu_dev->block_info.discard_granularity = UFS_BLOCK_SIZE;
    lu_dev->block_info.discard_zeroes_data = 0;
    lu_dev->block_info.discard_max_segments = 1;
#if 0 // support secure erase
    lu_dev->block_info.secure_erase_granularity = UFS_BLOCK_SIZE;
    lu_dev->block_info.secure_erase_max_bytes = DISCARD_MAX_BYTES;
    lu_dev->block_info.secure_erase_max_segments = 1;
#endif
#if 0 // support write zeroes
    lu_dev->block_info.write_zeroes_max_bytes = DISCARD_MAX_BYTES;
    lu_dev->block_info.write_zeroes_max_segments = 1;
#endif
}

static zx_status_t ufs_bind(void* ctx, zx_device_t* parent) {
    mtk_ufs_t* ufs = calloc(1, sizeof(mtk_ufs_t));
    if (!ufs) {
        return ZX_ERR_NO_MEMORY;
    }

    UFS_LOGI("ufs_bind\n");
    zx_status_t status = ZX_OK;
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV,
                                 (void*)&ufs->pdev);
    if (status != ZX_OK) {
        UFS_LOGE("ZX_PROTOCOL_PLATFORM_DEV not available\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS,
                                 (void*)&ufs->pbus);
    if (status != ZX_OK) {
        UFS_LOGE("ZX_PROTOCOL_PLATFORM_BUS not available\n");
        goto fail;
    }

    status = pdev_get_device_info(&ufs->pdev, &ufs->dev_info);
    if (status != ZX_OK) {
        UFS_LOGE("pdev_get_device_info failed\n");
        goto fail;
    }

    mtx_init(&ufs->lock, mtx_plain);
    list_initialize(&ufs->trans_list);
    status = zx_event_create(0, &ufs->worker_event);
    if (status != ZX_OK) {
        UFS_LOGE("sdmmc: failed to create event, retcode = %d\n", status);
        goto fail;
    }
    size_t size;
    status = pdev_map_mmio(&ufs->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                           &ufs->hba.hci_base, &size, &ufs->vmo_handle);
    if (status != ZX_OK) {
        UFS_LOGE("pdev_map_mmio_buffer failed\n");
        goto fail;
    }

    status = pdev_map_mmio(&ufs->pdev, 1, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                           &ufs->hba.mphy_base, &size, &ufs->mphy_vmo_handle);
    if (status != ZX_OK) {
        UFS_LOGW("pdev_map_mmio mphy failed: %d\n", status);
        ufs->hba.mphy_base = NULL;
    }

    status = pdev_get_bti(&ufs->pdev, 0, &ufs->bti_handle);
    // get the system cache line for hw DMA alignment required
    ufs->cache_line_size = zx_system_get_dcache_line_size();

    // initial ufs DMA used buffers(internal)
    ufs_dma_buf_init(ufs);

    ufs->crypto_profile = &block_proto.crypto_profile;
    status = ufs_init(ufs);
    if (status) {
        UFS_LOGE("ufs: failed to probe (%d)\n", status);
        goto fail;
    }

    ufs->state = UFS_PM_ACTIVE;

    for (uint32_t lun = 0; lun < UFS_LU_INTERNAL_CNT; lun++) {
        ufs_lu_dev_t* lu_dev = calloc(1, sizeof(*lu_dev));
        if (!lu_dev) {
            status = ZX_ERR_NO_MEMORY;
            goto fail_remove;
        }

        lu_dev->ufs = ufs;
        lu_dev->lun = lun;
        ufs_init_lu_block_info(ufs, lu_dev);

        char name[64];
        snprintf(name, sizeof(name), "sd%c", 'a' + lun);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = lu_dev,
            .ops = &ufs_device_proto,
            .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
            .proto_ops = &block_proto,
            .flags = DEVICE_ADD_INVISIBLE | DEVICE_ADD_MUST_ISOLATE,
            //.flags = DEVICE_ADD_NON_BINDABLE,
        };
        status = device_add(parent, &args, &lu_dev->zxdev);
        if (status != ZX_OK) {
            free(lu_dev);
            goto fail_remove;
        }

        ufs->lu_devs[lun] = lu_dev;
        ufs->lu_dev_count++;
        UFS_LOGI("ufs: bind LU%u as %s, block_count: %" PRIu64 "\n",
                 lun, name, lu_dev->block_info.block_count);
    }

    device_add_args_t vrpmb_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vrpmb",
        .ctx = ufs,
        .ops = &ufs_vrpmb_device_proto,
    };
    status = device_add(ufs->lu_devs[UFS_LU_2]->zxdev, &vrpmb_args, &ufs->vrpmb_zxdev);
    if (status != ZX_OK) {
        goto fail_remove;
    }

    status = thrd_create_with_name(&ufs->worker_thread, ufs_worker_thread,
                                   ufs, "ufs-worker");
    if (status != thrd_success) {
        status = thrd_status_to_zx_status(status);
        goto fail_remove;
    }
    ufs->worker_thread_running = true;
    thrd_detach(ufs->worker_thread);

    ufs->ufs_proto.ops = &ufs_protocol_ops;
    ufs->ufs_proto.ctx = ufs;
    // notify bus that ufs is ready to go
#if 0
    status = pbus_set_protocol(&ufs->pbus, ZX_PROTOCOL_UFS, &ufs->ufs_proto);
    if (status != ZX_OK) {
        UFS_LOGE("pbus_set_protocol failed(%d)\n", status);
        goto fail_remove;
    }
#endif
    return ZX_OK;

fail_remove:
    if (ufs->vrpmb_zxdev) {
        device_remove(ufs->vrpmb_zxdev);
        ufs->vrpmb_zxdev = NULL;
    }
    bool lu_devs_added = ufs->lu_dev_count > 0;
    for (uint32_t i = 0; i < UFS_LU_INTERNAL_CNT; i++) {
        if (ufs->lu_devs[i] && ufs->lu_devs[i]->zxdev) {
            device_remove(ufs->lu_devs[i]->zxdev);
        }
    }
    if (lu_devs_added) {
        return status;
    }
fail:
    if (ufs) {
        ufs_dma_buf_free(ufs);
        free(ufs);
    }
    return status;
}

static zx_driver_ops_t ufs_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ufs_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(ufs, ufs_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_UFS),
ZIRCON_DRIVER_END(ufs)
// clang-format on
