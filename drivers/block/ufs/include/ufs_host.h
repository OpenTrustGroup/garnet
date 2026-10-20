// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ufs_hcd.h"

typedef struct ufs_protocol_ops {
    // returns ufs controller info
    zx_status_t (*get_info)(void* ctx, zx_handle_t* handle_out);
} ufs_protocol_ops_t;

typedef struct ufs_protocol {
    ufs_protocol_ops_t* ops;
    void* ctx;
} ufs_protocol_t;

#define UFS_CHUNK_STAT_MAX 16U

typedef struct ufs_lu_dev ufs_lu_dev_t;

// block io transactions. one per client request
typedef struct ufs_trans {
    block_op_t bop;
    list_node_t node;
    ufs_lu_dev_t* lu_dev;
    zx_paddr_t *phys_list;
    zx_handle_t pmt;
    zx_handle_t sg_pmts[BLOCK_OP_MAX_SG_REGIONS];
    uint32_t sg_pmt_count;
    unsigned int compl_size;
} ufs_trans_t;

#define to_ufs_trans(_bop)	\
    containerof(_bop, ufs_trans_t, bop);

typedef struct ufs_iobuf {
    // input
    size_t size;
    uint64_t align;
    uint32_t flags;

    // output
    io_buffer_t iob;
    void* virt;
    zx_paddr_t phys;
} ufs_iobuf_t;

typedef enum ufs_pm_state {
    UFS_PM_POWEROFF = 0,
    UFS_PM_SUSPEND,
    UFS_PM_ACTIVE,
} ufs_pm_state_t;

typedef struct mtk_ufs {
    platform_bus_protocol_t pbus;
    platform_device_protocol_t pdev;
    pdev_device_info_t dev_info;
    zx_device_t* vrpmb_zxdev;
    void* hci_base;
    zx_handle_t vmo_handle;
    zx_handle_t mphy_vmo_handle;
    zx_handle_t bti_handle;

    ufs_protocol_t ufs_proto;

    struct ufs_hba hba;

    mtx_t lock;

    // blockio requests
    list_node_t bop_list;

    thrd_t worker_thread;
    zx_handle_t worker_event;
#define UFS_TRANS_RECEIVED  ZX_USER_SIGNAL_0
#define UFS_SHUTDOWN        ZX_USER_SIGNAL_1
#define UFS_SHUTDOWN_DONE   ZX_USER_SIGNAL_2
#define UFS_EH_DONE         ZX_USER_SIGNAL_3
#define UFS_STATE_CHANGED   ZX_USER_SIGNAL_4
    bool worker_thread_running;
    ufs_pm_state_t state;
    bool worker_waiting;
    bool direct_submit_active;

    list_node_t trans_list;

    ufs_iobuf_t ucdl;
    ufs_iobuf_t utrdl;

    uint32_t cache_line_size;
    struct blk_crypto_profile **crypto_profile;

    ufs_lu_dev_t* lu_devs[UFS_LU_INTERNAL_CNT];
    uint32_t lu_dev_count;
    uint32_t lu_release_count;
} mtk_ufs_t;

struct ufs_lu_dev {
    mtk_ufs_t* ufs;
    zx_device_t* zxdev;
    uint32_t lun;
    block_info_t block_info;
};

static inline mtk_ufs_t* hba_to_ufs(struct ufs_hba* hba) {
    return containerof(hba, mtk_ufs_t, hba);
}

int ufs_mask_interrupt(struct ufs_hba* hba, uint32_t vector);
int ufs_unmask_interrupt(struct ufs_hba* hba, uint32_t vector);
zx_status_t ufs_alloc_iobuf(mtk_ufs_t* ufs, ufs_iobuf_t* iob);
void ufs_free_iobuf(ufs_iobuf_t* iob);
int ufshcd_init(mtk_ufs_t* ufs);
void ufs_dma_map(void* buf, size_t len, enum dma_data_direction dir);
void ufs_dma_unmap(void* buf, size_t len, enum dma_data_direction dir);
zx_status_t ufshcd_irq_init(struct ufs_hba *hba);
void ufshcd_complete_bop(struct ufs_hba *hba, struct ufshcd_lrb *lrbp);
void ufs_wake_worker_thread(struct ufs_hba *hba);
