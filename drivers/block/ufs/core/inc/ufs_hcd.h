/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copyright (c) 2019 MediaTek Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <threads.h>
#include <hw/reg.h>
#include <zircon/device/blk_crypto.h>
#include <ddk/protocol/block.h>

#include "ufs_hci.h"
#include "ufs.h"
//#include "ufs_protocol.h"
#include "ufs_sys.h"

#define UFS_HW_CRYPT_EN

// SCSI commands
#define INQUIRY         0x12
#define READ_CAPACITY   0x25
#define READ_10         0x28
#define WRITE_10        0x2A
#define SYNC_CACHE_10   0x35
#define WRITE_BUFFER    0x3B
#define UNMAP           0x42
#define SECURITY_PROTOCOL_OUT           0xB5
#define SECURITY_PROTOCOL_IN            0xA2

/*
 *  SENSE KEYS
 */
#define UNIT_ATTENTION      0x06

// DMA related configurations
#define UFS_MAX_SIZE_PER_SG_SEGMENT (256 * 1024)    // UFSHCI 2.1 spec: 256 KB

#define UFS_MAX_NUTRS               (32) // number of UTP Transfer Requset Slot
#define UFS_MAX_NUTMRS              (1) // number of UTP Task Management Requset Slot

// alignment requirement
// UTP command descriptors

#define UFS_ALIGN_UCD(h)                ALIGN(1024, (h)->cache_line_size)
// UTP transfer request descriptors. 32 is enough for UFSHCI 2.1 spec after applying SW trick
#define UFS_ALIGN_UTRD(h)               ALIGN(1024, (h)->cache_line_size)
// UTP task management descriptors. 32 is enough for UFSHCI 2.1 spec after applying SW trick
#define UFS_ALIGN_UTMRD(h)              ALIGN(32, (h)->cache_line_size)

#define UFS_BLOCK_BITS                  (12)
#define UFS_BLOCK_SIZE                  (1 << UFS_BLOCK_BITS)   // 4 KB

#define UFS_1KB                         (0x400)
#define UFS_1KB_MASK                    (UFS_1KB - 1)

/*
 * ufs_utrdl length need align cache line, else if any data below ufs_utrdl
 * in map file dirty, will casue all cache line dirty. SW invalid ufs_utrdl
 * may overwrite DMA HW updated data in OCS.
 */
#define UTRD_SIZE       (sizeof(struct utp_transfer_req_desc))
#define UTRD_NUTRS(h)      ((UTRD_SIZE+(h)->cache_line_size-1) / UTRD_SIZE)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

// Original UFSHCD_ENABLE_INTRS (defined in kernel) does not have UIC_POWER_MODE!
#define UFSHCD_ENABLE_INTRS    (UTP_TRANSFER_REQ_COMPL | \
                 UTP_TASK_REQ_COMPL | \
                 UIC_POWER_MODE | \
                 UIC_HIBERNATE_ENTER | \
                 UIC_HIBERNATE_EXIT | \
                 UFSHCD_ERROR_MASK)

/* UIC command timeout, unit: ms */
#define UIC_CMD_TIMEOUT    500

/* NOP OUT retries waiting for NOP IN response */
#define NOP_OUT_RETRIES    10
/* Timeout after 30 msecs if NOP OUT hangs without response */
#define NOP_OUT_TIMEOUT    100 /* msecs */

/* Query request retries */
#define QUERY_REQ_RETRIES 10
/* Query request timeout */
#define QUERY_REQ_TIMEOUT 1700 /* msec */

/* Task management command timeout */
#define TM_CMD_TIMEOUT    1300 /* msecs */

/* maximum number of link-startup retries */
#define DME_LINKSTARTUP_RETRIES 3

/* maximum number of reset retries before giving up */
#define MAX_HOST_RESET_RETRIES 5

/* Hynix device need max 5 seconds to clear fDeviceInit, we set 5s here to provide some margin */
#define UFS_FDEVICEINIT_TIMEOUT_US ZX_SEC(5)

/* Get tag retry timeout 5 seconds*/
#define UFS_GETTAG_TIMEOUT_US ZX_SEC(5)

/* maximum number of test unit ready before giving up */
#define UFS_TEST_UNIT_READY_TIMEOUT_US    (15000)

/* Expose the flag value from utp_upiu_query.value */
#define MASK_QUERY_UPIU_FLAG_LOC 0xFF

/* Interrupt aggregation default timeout, unit: 40us */
#define INT_AGGR_DEF_TO    0x02

#define UTP_TRANSFER_REQ_TIMEOUT_MS (5000)  // 5 seconds for a single UTP transfer request
// TODO: need different timeout for different operations

#define UTP_UNMAP_TIMEOUT_MS (60000)

#define ufshcd_writel(hba, val, reg) \
    writel((val), (hba)->hci_base + (reg))
#define ufshcd_readl(hba, reg) \
    readl((hba)->hci_base + (reg))

#define ufsphy_writel(hba, val, reg) \
    writel((val), (hba)->mphy_base + (reg))
#define ufsphy_readl(hba, reg) \
    readl((hba)->mphy_base + (reg))

/*
 * This quirk needs to be enabled for MTK MPHY test chip.
 */
#define UFSHCD_QUIRK_MTK_MPHY_TESTCHIP          UFS_BIT(31)

enum {
    SW_RST_TARGET_HCI         = 0x1,
    SW_RST_TARGET_UNIPRO      = 0x2,
    SW_RST_TARGET_CPT         = 0x4,    // crypto engine
    SW_RST_TARGET_MPHY        = 0x8,
    SW_RST_TARGET_MAC_ALL     = (SW_RST_TARGET_HCI | SW_RST_TARGET_UNIPRO | SW_RST_TARGET_CPT),
};

enum {
    ATTR_SIMPLE        = 0,
    ATTR_ORDERED       = 1,
    ATTR_HEAD_OF_QUEUE = 2,
};

enum dev_cmd_type {
    DEV_CMD_TYPE_NOP          = 0x0,
    DEV_CMD_TYPE_QUERY        = 0x1,
};

enum dma_data_direction {
    DMA_BIDIRECTIONAL = 0,
    DMA_TO_DEVICE     = 1,
    DMA_FROM_DEVICE   = 2,
    DMA_NONE          = 3,
};

enum ufs_logical_unit_internal {
    UFS_LU_0            = 0,
    UFS_LU_1            = 1,
    UFS_LU_2            = 2,
    UFS_LU_INTERNAL_CNT = 3
};

/*
 * UFS driver/device status
 */
#define UFS_DRV_STS_TEST_UNIT_READY_ALL_DEVICE  (0x00000001)
#define UFS_DRV_STS_DMA_WAIT_INTERRUPT          (0x00000002)

/**
 * struct uic_command - UIC command structure
 * @command: UIC command
 * @argument1: UIC command argument 1
 * @argument2: UIC command argument 2
 * @argument3: UIC command argument 3
 * @cmd_active: Indicate if UIC command is outstanding
 * @result: UIC command result
 * @done: UIC command completion
 */
struct uic_command {
    u32 command;
    u32 argument1;
    u32 argument2;
    u32 argument3;
    int cmd_active;
    int result;
};

/**
 * struct ufshcd_sg_entry - UFSHCI PRD Entry
 * @base_addr: Lower 32bit physical address DW-0
 * @upper_addr: Upper 32bit physical address DW-1
 * @reserved: Reserved for future use DW-2
 * @size: size of physical segment DW-3
 */
struct ufshcd_sg_entry {
    __le32    base_addr;
    __le32    upper_addr;
    __le32    reserved;
    __le32    size;
};

/**
 * struct ufs_query - holds relevant data structures for query request
 * @request: request upiu and function
 * @descriptor: buffer for sending/receiving descriptor
 * @response: response upiu and response
 */
struct ufs_query {
    struct ufs_query_req request;
    u8 *descriptor;
    struct ufs_query_res response;
};

/**
 * struct ufshcd_lrb - local reference block
 * @utr_descriptor_ptr: UTRD address of the command
 * @ucd_req_ptr: UCD address of the command
 * @ucd_rsp_ptr: Response UPIU address for this command
 * @ucd_prdt_ptr: PRDT address of the command
 * @cmd: pointer to SCSI command
 * @sense_buffer: pointer to sense buffer address of the SCSI command
 * @sense_bufflen: Length of the sense buffer
 * @scsi_status: SCSI status of the command
 * @command_type: SCSI, UFS, Query.
 * @task_tag: Task tag of the command
 * @lun: LUN of the command
 * @intr_cmd: Interrupt command (doesn't participate in interrupt aggregation)
 */
struct ufshcd_lrb {
    struct utp_transfer_req_desc *utr_descriptor_ptr;
    struct utp_upiu_req *ucd_req_ptr;
    struct utp_upiu_rsp *ucd_rsp_ptr;
    struct ufshcd_sg_entry *ucd_prdt_ptr;

    block_op_t *bop;
    struct ufs_scsi_cmd *cmd;
    u8 *sense_buffer;
    unsigned int sense_bufflen;
    int scsi_status;

    int command_type;
    int task_tag;
    u8 lun; /* UPIU LUN id field is only 8-bit wide */

    bool intr_cmd;
    /* For request from block set to false otherwise true */
    bool sync_cmd;
    int crypto_key_slot;
    u64 data_unit_num;

    uint64_t exp_len;
    void * iob_unmap; //for unmap param list
};

struct ufs_bop {
    block_op_t *bop;
    uintptr_t *phys_list;
    zx_handle_t pmt;
    unsigned int compl_size;
};

struct ufs_pa_layer_attr {
    u32 gear_rx;
    u32 gear_tx;
    u32 lane_rx;
    u32 lane_tx;
    u32 pwr_rx;
    u32 pwr_tx;
    u32 hs_rate;
};

struct ufs_pwr_mode_info {
    bool is_valid;
    struct ufs_pa_layer_attr info;
};

struct ufs_dma_sg_entry {
    uintptr_t paddr;
    uint32_t len;
};

struct ufs_scsi_cmd {
    unsigned int lun;
    int tag;
    enum dma_data_direction dir;
    unsigned char attr;
    unsigned char cmd_data[MAX_CDB_SIZE];
    unsigned short cmd_len;
    unsigned int exp_len;
    uintptr_t paddr;
    const struct ufs_dma_sg_entry* sg;
    uint32_t sg_count;
};

struct ufs_unmap_block_descriptor { // 16 bytes
    u8 unmap_logical_block_address[8];
    u8 num_logical_blocks[4];
    u8 reserved[4];
};

struct ufs_unmap_param_list {       // 24 bytes
    u8 unmap_data_length[2];
    u8 unmap_block_descriptor_data_length[2];
    u8 reserved[4];
    struct ufs_unmap_block_descriptor unmap_block_descriptor;
};

#define UFS_MAX_CMD_DATA_SIZE   (64)

/**
 * struct ufs_dev_cmd - all assosiated fields with device management commands
 * @type: device management command type - Query, NOP OUT
 */
struct ufs_dev_cmd {
    enum dev_cmd_type type;
    struct ufs_query query;
};

#define MAX_PRODUCT_ID_LEN              (16)
#define MAX_PRODUCT_REVISION_LEVEL_LEN  (4)
#define MAX_SERAL_NUMBER_LEN            (64) /* spec (126*2), 64 because code size */

struct ufs_device_info {
    u16 wmanufacturerid;                     // from Device Descriptor
    u8  num_active_lu;                       // from Device Descriptor
    u16 ufs_ver;                             // from Device Descriptor
    u8	max_lu_supported;                    // from Device Descriptor
    u8  bootable;
    char product_id[MAX_PRODUCT_ID_LEN + 1];
    char product_revision_level[MAX_PRODUCT_REVISION_LEVEL_LEN + 1];
    char serial_number[MAX_SERAL_NUMBER_LEN * 2 + 1]; /* 1 byte need 2 char(ex.FF) + 1 end */
    u8  serial_number_len;
    u8  ud0_base_offset;
    u8  ud_config_len;
    u8  hpb_support;
    u16 hpb_ver;
    u8  hpb_ctrl_mode;
    u8  tw_support;
    u8  tw_red;
    u8  tw_type;
    u16 tw_ver;
    /* UFS WB related flags */
	bool  wb_enabled;
	bool  wb_buf_flush_enabled;
	u8	wb_dedicated_lu;
	u8  wb_buffer_type;
    u32 wb_buf_au;
    u8	b_presrv_uspc_en;
    u8 pre_eol_info;
    u8 life_time_est_a;
    u8 life_time_est_b;
    u64 rpmb_lu_size; /* RPMB lu size in bytes */
    u32 rpmb_rw_size; /* Maximun number of rpmb frames allowed in single upiu */
};

struct ufs_geometry_info {
    u8  b_allocation_units_size;     // number of segment
    u16 w_adj_factor_system_code;
    u16 w_adj_factor_non_persist;
    u16 w_adj_factor_enahnced_1;
    u16 w_adj_factor_enahnced_2;
    u16 w_adj_factor_enahnced_3;
    u16 w_adj_factor_enahnced_4;
    u32 d_segment_size;              // in 512 bytes
    u64 q_total_raw_device_capacity; // in 512 bytes
    u16 w_hpb_device_max_active_regions;
    u16 w_hpb_lu_max_active_regions;
    u64 q_hpb_region_size;           // in 512 bytes
    u32 d_tw_buf_max_au;
    u32 d_tw_buf_au;
    u8  b_tw_buf_adj_fac;
    u8  b_tw_support_red_type;
    u8  b_tw_support_buf_type;
};

struct ufs_unit_desc_cfg_param {
    u8 b_lu_enable;
    u8 b_boot_lun_id;
    u8 b_lu_write_protect;
    u8 b_memory_type;
    u8 d_num_alloc_units[4];
    u8 b_data_reliability;
    u8 b_logical_block_size;
    u8 b_provisioning_type;
    u8 w_context_capabilities[2];
    u8 reserved[3];
};

#define UFS_EVENT_HIST_LENGTH 8
/**
 * struct ufs_event_hist - keeps history of errors
 * @pos: index to indicate cyclic buffer position
 * @val: cyclic buffer for registers value
 * @tstamp: cyclic buffer for time stamp
 * @cnt: error counter
 */
struct ufs_event_hist {
	int pos;
	u32 val[UFS_EVENT_HIST_LENGTH];
	u64 tstamp[UFS_EVENT_HIST_LENGTH];
	unsigned long long cnt;
};

/**
 * struct ufs_stats - keeps usage/err statistics
 * @last_intr_status: record the last interrupt status.
 * @last_intr_ts: record the last interrupt timestamp.
 * @hibern8_exit_cnt: Counter to keep track of number of exits,
 *		reset this after link-startup.
 * @last_hibern8_exit_tstamp: Set time after the hibern8 exit.
 *		Clear after the first successful command completion.
 * @event: array with event history.
 */
struct ufs_stats {
	u32 last_intr_status;
	u64 last_intr_ts;

	u32 hibern8_exit_cnt;
	u64 last_hibern8_exit_tstamp;
	struct ufs_event_hist event[UFS_EVT_CNT];
};

struct ufs_hba {
    void    *hci_base;
    void    *pericfg_base;
    void    *mphy_base;
    int     nutrs;
    //int     nutmrs;
    bool    initready;
    bool    wb_support;
    bool    ref_clk_enabled;
    bool    mphy_powered_on;
    bool    unipro_lpm;
    u16     ref_clk_gating_wait_us;
    u16     ref_clk_ungating_wait_us;

    /* Virtual memory reference */
    struct utp_transfer_cmd_desc *ucdl_base_addr;
    struct utp_transfer_req_desc *utrdl_base_addr;

    /* DMA memory reference */
    uintptr_t ucdl_dma_addr;
    uintptr_t utrdl_dma_addr;

    unsigned int hci_quirks;
    unsigned int dev_quirks;

    bool auto_bkops_enabled;
    bool is_urgent_bkops_lvl_checked;
    enum bkops_status urgent_bkops_lvl;

    u32 critical_health_count;

    u32 ee_ctrl_mask;
    u16 ee_drv_mask;
    u16 ee_usr_mask;
    mutex_t ee_ctrl_mutex;

    mutex_t free_tag_lock;
    mutex_t outstanding_lock;
    mutex_t bop_bitmap_lock;
    completion_t tag_release;

    struct uic_command *active_uic_cmd;
    struct ufs_pa_layer_attr pwr_info;

    struct ufshcd_lrb *lrb;
    unsigned long lrb_in_use;
    unsigned long bop_bitmap;

    struct ufs_device_info dev_info;

    u8  active_tr_tag;
    u8  mode;
    //u8  active_tm_tag;
    int  active_lun;

    unsigned long outstanding_reqs;

    struct ufs_pwr_mode_info max_pwr_info;

    u8  quirk_sel;  /* selector. legacy Samsung and Micron dev takes selector 0x01 */

    /* Device management request data */
    struct ufs_dev_cmd dev_cmd;

    thrd_t irq_thread;
    thrd_t eh_thread;
    zx_handle_t irq_event;
    zx_handle_t irq_handle;
    zx_handle_t eh_event;
    u32 irq_status;
    bool eh_thread_running;

    /* HBA Errors */
    u32 errors;
    u32 uic_error;
    u32 saved_err;
    u32 saved_uic_err;
    struct ufs_stats ufs_stats;
    bool force_reset;
    u32 eh_flags;

#ifdef UFS_HW_CRYPT_EN
     /* Crypto related fields */
    union ufs_crypto_capabilities crypto_capabilities;
    union ufs_crypto_cap_entry *crypto_cap_array;
    u32 crypto_cfg_register;
    struct blk_crypto_profile_dev crypto_profile_dev;
#endif

    int (*blk_read)(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt, uintptr_t pa);
    int (*blk_write)(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt, uintptr_t pa);
    int (*blk_erase)(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt);
    int (*blk_secure_erase)(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt);
    int (*blk_write_zeroes)(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt);
    int (*blk_flush)(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt);

    int (*nopin_nopout)(struct ufs_hba *hba);
    int (*query_flag)(struct ufs_hba *hba, enum query_opcode opcode, enum flag_idn idn,int desc_index, bool *flag_res);
    int (*query_attr)(struct ufs_hba *hba, enum query_opcode opcode, enum attr_idn idn,
        u8 index, u8 selector, u32 *attr_val);
    int (*read_descriptor)(struct ufs_hba *hba, enum desc_idn desc_id, int desc_index,
        u8 selector, u8 *buf, u32 size);
    int (*write_descriptor)(struct ufs_hba *hba, enum desc_idn desc_id, int desc_index,
        u8 selector, u8 *buf, u32 size);
    int (*dme_get)(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val);
    int (*dme_peer_get)(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val);
    int (*dme_set)(struct ufs_hba *hba, u32 attr_sel, u32 mib_val);
    int (*dme_peer_set)(struct ufs_hba *hba, u32 attr_sel, u32 mib_val);
    int (*ffu_write)(struct ufs_hba *hba, uintptr_t pa, u32 buf_size);
    int (*set_dev_pwr)(struct ufs_hba *hba, enum ufs_dev_pwr_mode pwr_mode);

    u32 blk_cnt[UFS_LU_INTERNAL_CNT];
    mutex_t lock;    /* mutex lock for multi-thread */

    u32 drv_status;
    u32 irq;
    pdev_irq_t out_irq;
};
#define crypto_profile_dev_to_hba(dev_profile)		\
containerof(dev_profile, struct ufs_hba, crypto_profile_dev)

#define ufs_writel(hba, val, reg)    \
    writel((val), (hba)->hci_base + (reg))

#define ufs_readl(hba, reg)    \
    readl((hba)->hci_base + (reg))

#ifndef BITS_PER_LONG
    #if __SIZEOF_LONG__ == 8
        #define BITS_PER_LONG 64
    #else
        #define BITS_PER_LONG 32
    #endif
#endif

static inline void set_bit(int nr, unsigned long *addr) {
    addr[nr / BITS_PER_LONG] |= (1UL << (nr % BITS_PER_LONG));
}

static inline unsigned long find_next_bit(const unsigned long *addr, unsigned long size, unsigned long offset) {
    if (offset >= size) {
        return size;
    }

    unsigned long word_idx = offset / BITS_PER_LONG;
    unsigned long bit_offset = offset % BITS_PER_LONG;
    const unsigned long *p = addr + word_idx;
    unsigned long tmp;

    if (bit_offset) {
        tmp = *p & (~0UL << bit_offset); 
        if (tmp) {
            return (word_idx * BITS_PER_LONG) + (__builtin_ffsl(tmp) - 1);
        }
        word_idx++;
        p++;
    }

    while (word_idx < size / BITS_PER_LONG) {
        if (*p) {
            return (word_idx * BITS_PER_LONG) + (__builtin_ffsl(*p) - 1);
        }
        word_idx++;
        p++;
    }

    if (size % BITS_PER_LONG) {
        tmp = *p & (~0UL >> (BITS_PER_LONG - (size % BITS_PER_LONG)));
        if (tmp) {
            return (word_idx * BITS_PER_LONG) + (__builtin_ffsl(tmp) - 1);
        }
    }

    return size;
}

static inline unsigned long find_first_bit(const unsigned long *addr, unsigned long size) {
    return find_next_bit(addr, size, 0);
}

#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit((addr), (size)); \
         (bit) < (size); \
         (bit) = find_next_bit((addr), (size), (bit) + 1))

/*
 * Details of UIC Errors
 */
static const char *const ufs_uic_err_str[] = {
	"PHY Adapter Layer",
	"Data Link Layer",
	"Network Link Layer",
	"Transport Link Layer",
	"DME"
};

static const char *const ufs_uic_pa_err_str[] = {
	"PHY error on Lane 0",
	"PHY error on Lane 1",
	"PHY error on Lane 2",
	"PHY error on Lane 3",
	"Generic PHY Adapter Error. This should be the LINERESET indication"
};

static const char *const ufs_uic_dl_err_str[] = {
	"NAC_RECEIVED",
	"TCx_REPLAY_TIMER_EXPIRED",
	"AFCx_REQUEST_TIMER_EXPIRED",
	"FCx_PROTECTION_TIMER_EXPIRED",
	"CRC_ERROR",
	"RX_BUFFER_OVERFLOW",
	"MAX_FRAME_LENGTH_EXCEEDED",
	"WRONG_SEQUENCE_NUMBER",
	"AFC_FRAME_SYNTAX_ERROR",
	"NAC_FRAME_SYNTAX_ERROR",
	"EOF_SYNTAX_ERROR",
	"FRAME_SYNTAX_ERROR",
	"BAD_CTRL_SYMBOL_TYPE",
	"PA_INIT_ERROR",
	"PA_ERROR_IND_RECEIVED",
	"PA_INIT"
};

/**
 * ufs_hba_stop - Send controller to reset state
 * @hba: per adapter instance
 */
static inline void ufs_hba_stop(struct ufs_hba *hba)
{
    ufs_writel(hba, CONTROLLER_DISABLE,  REG_CONTROLLER_ENABLE);
}

/**
 * ufs_hba_start - Start controller initialization sequence
 * @hba: per adapter instance
 */
static inline void ufs_hba_start(struct ufs_hba *hba)
{
	u32 val = CONTROLLER_ENABLE;

	val |= CRYPTO_GENERAL_ENABLE;

    ufshcd_writel(hba, val, REG_CONTROLLER_ENABLE);
}

#define UIC_COMMAND(cmd, attr, index, value) \
( \
    (struct uic_command) { \
        .command = (cmd), \
        .argument1 = UIC_ARG_MIB_SEL(attr, index), \
        .argument2 = 0, \
        .argument3 = (value), \
        .result = 0xff, \
    } \
)

#define UIC_CMD_DME_GET(attr, index, value) \
        UIC_COMMAND(UIC_CMD_DME_GET, attr, index, value)

#define UIC_CMD_DME_PEER_GET(attr, index, value) \
        UIC_COMMAND(UIC_CMD_DME_PEER_GET, attr, index, value)

#define UIC_CMD_DME_SET(attr, index, value) \
        UIC_COMMAND(UIC_CMD_DME_SET, attr, index, value)

#define UIC_CMD_DME_PEER_SET(attr, index, value) \
        UIC_COMMAND(UIC_CMD_DME_PEER_SET, attr, index, value)

/* UIC command interfaces for DME primitives */
#define DME_LOCAL    0
#define DME_PEER    1
#define ATTR_SET_NOR    0    /* NORMAL */
#define ATTR_SET_ST    1    /* STATIC */

/*------------------------------------------------
 * UFS quirks related definitions and declarations
 *------------------------------------------------
 */

/* return true if s1 is a prefix of s2 */
#define STR_PRFX_EQUAL(s1, s2) !strncmp(s1, s2, strlen(s1))

#define UFS_ANY_VENDOR 0xFFFF
#define UFS_ANY_MODEL  "ANY_MODEL"

#define UFS_VENDOR_MICRON_MP	0x12C
#define UFS_VENDOR_MICRON_ES	0x02C
#define UFS_VENDOR_TOSHIBA	0x198
#define UFS_VENDOR_SAMSUNG	0x1CE
#define UFS_VENDOR_SKHYNIX	0x1AD
#define UFS_VENDOR_WDC		0x145

/**
 * ufs_dev_fix - ufs device quirk info
 * @card: ufs card details
 * @quirk: device quirk
 */
struct ufs_dev_fix {
    u16 wmanufacturerid;
    char product_id[MAX_PRODUCT_ID_LEN + 1];
    unsigned int quirk;
};

#define END_FIX { 0 }

/* add specific device quirk */
#define UFS_FIX(_vendor, _model, _quirk) { \
    .wmanufacturerid = (_vendor),      \
    .product_id = (_model),            \
    .quirk = (_quirk),                 \
}

/*
 * Some UFS memory device needs to lock configuration descriptor after programming LU.
 */
#define UFS_DEVICE_QUIRK_NEED_LOCK_CONFIG_DESC              (1U << 31)

#define UFS_DEVICE_QUIRK_NEED_CONFIGURATION_FOR_UNUSED_LU   (1U << 30)

#define UFS_DEVICE_QUIRK_NEED_8_BYTE_ALIGNED_BUF            (1U << 29)

/*
 * Some UFS memory device needs limited RPMB max rw size otherwise
 * device issue, for example, device hang, may happen in some scenarios.
 */
#define UFS_DEVICE_QUIRK_LIMITED_RPMB_MAX_RW_SIZE           (1U << 28)

#define UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE                  (1U << 7)
#define UFS_DEVICE_QUIRK_PA_TACTIVATE                       (1U << 4)
#define UFS_DEVICE_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL          (1U << 12)

#define UFS_RPMB_DEV_MAX_RW_SIZE_LIMITATION (8)

/* UFSHCD error handling flags */
enum {
	UFSHCD_EH_IN_PROGRESS = (1 << 0),
};
#define ufshcd_set_eh_in_progress(h) \
	((h)->eh_flags |= UFSHCD_EH_IN_PROGRESS)
#define ufshcd_eh_in_progress(h) \
	((h)->eh_flags & UFSHCD_EH_IN_PROGRESS)
#define ufshcd_clear_eh_in_progress(h) \
	((h)->eh_flags &= ~UFSHCD_EH_IN_PROGRESS)
/*
 * UFS crypto related
 */

enum {
    UFS_CRYPTO_ALGO_AES_XTS             = 0,
    UFS_CRYPTO_ALGO_BITLOCKER_AES_CBC   = 1,
    UFS_CRYPTO_ALGO_AES_ECB             = 2,
    UFS_CRYPTO_ALGO_ESSIV_AES_CBC       = 3,
};

enum {
    UFS_CRYPTO_DATA_UNIT_SIZE_4KB       = 3,
};

union ufs_cpt_cap {
    u32 cap_raw;
    struct {
        u8 cap_cnt;
        u8 cfg_cnt;
        u8 resv;
        u8 cfg_ptr;
    } cap;
};

union ufs_cpt_capx {
    u32 capx_raw;
    struct {
        u8 alg_id;
        u8 du_size;
        u8 key_size;
        u8 resv;
    } capx;
};

union ufs_cap_cfg {
    u32 cfgx_raw[32];
    struct {
        u32 key[16];
        u8 du_size;
        u8 cap_id;
        u16 resv0  : 15;
        u16 cfg_en : 1;
        u8 mu1ti_host;
        u8 resv1;
        u16 vsb;
        u32 resv2[14];
    } cfgx;
};

#define UPIU_COMMAND_CRYPTO_EN_OFFSET   23

enum UFS_MTCMOS_MODE {
	SIDEBAND = 0,
	PBUS_HW_MODE = 1,
	PBUS_SW_MODE = 2,
};

/* UFS RPMB HMAC operation */
enum {
    RPMB_HMAC_INIT = 0,
    RPMB_HMAC_DEINIT,
    RPMB_HMAC_CALC
};


/**
 * Exported API declaratoins
 */
int ufs_dma_nopin_nopout(struct ufs_hba *hba);

int ufs_dma_query_flag(struct ufs_hba *hba, enum query_opcode opcode,
    enum flag_idn idn, int index, bool *flag_res);

int ufs_dma_read_desc(struct ufs_hba *hba, enum desc_idn desc_id,
    int desc_index, u8 selector, u8 *buf, u32 size);

int ufs_dma_write_desc(struct ufs_hba *hba, enum desc_idn idn, int index,
    u8 selector, u8 *src_buf, u32 buf_len);

int ufs_dma_dme_get(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val);

int ufs_dma_dme_peer_get(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val);

int ufs_dma_dme_set(struct ufs_hba *hba, u32 attr_sel, u32 mib_val);

int ufs_dma_dme_peer_set(struct ufs_hba *hba, u32 attr_sel, u32 mib_val);

int ufs_dma_query_attr(struct ufs_hba *hba, enum query_opcode opcode,
    enum attr_idn idn, u8 index, u8 selector, u32 *attr_val);

int ufs_get_device_info(struct ufs_hba *hba);
int          ufs_get_lu_size(struct ufs_hba *hba, u32 lun, u32 *blk_size_in_byte,
    u32 *blk_cnt);
int ufs_set_boot_lu(struct ufs_hba *hba, u32 b_boot_lun_en);
int ufs_get_active_boot_part(u32 *active_boot_part);
int ufs_check_lu_cfg(struct ufs_hba *hba);
int ufs_poweroff_notify(struct ufs_hba *hba);
void ufs_reset_device_low(struct ufs_hba *hba);

struct ufs_hba *ufs_get_host(u8 host_id);

void ufs_init_mphy(void);
void ufs_set_bootdevice(void);
bool ufs_is_in_secure_world(void);

bool ufshcd_get_free_tag(struct ufs_hba *hba, int *tag_out);
void ufshcd_put_tag(struct ufs_hba *hba, int tag);

bool ufshcd_get_free_bop(struct ufs_hba *hba, int *pos);
void ufshcd_put_bop(struct ufs_hba *hba, int pos);

int ufshcd_queuecommand(struct ufs_hba *hba, struct ufs_scsi_cmd *cmd);

int ufshcd_dme_get_attr(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val, u8 peer);
int ufshcd_dme_set_attr(struct ufs_hba *hba, u32 attr_sel, u8 attr_set, u32 mib_val,
                                u8 peer);
int ufshcd_wait_command(struct ufs_hba *hba, struct ufshcd_lrb *lrbp, u32 timeout_ms);
void ufshcd_irq_handler(struct ufs_hba* hba);
void ufshcd_eh_exit(struct ufs_hba *hba);
int ufshcd_uic_hibern8_enter(struct ufs_hba *hba);
int ufshcd_uic_hibern8_exit(struct ufs_hba *hba);

/*
 * Vendor specific link state
 */
enum {
	VS_LINK_DISABLED            = 0,
	VS_LINK_DOWN                = 1,
	VS_LINK_UP                  = 2,
	VS_LINK_HIBERN8             = 3,
	VS_LINK_LOST                = 4,
	VS_LINK_CFG                 = 5,
};

/*
 * Vendor specific host controller state
 */
enum {
	VS_HCE_RESET                = 0,
	VS_HCE_BASE                 = 1,
	VS_HCE_OOCPR_WAIT           = 2,
	VS_HCE_DME_RESET            = 3,
	VS_HCE_MIDDLE               = 4,
	VS_HCE_DME_ENABLE           = 5,
	VS_HCE_DEFAULTS             = 6,
	VS_HIB_IDLEEN               = 7,
	VS_HIB_ENTER                = 8,
	VS_HIB_ENTER_CONF           = 9,
	VS_HIB_MIDDLE               = 10,
	VS_HIB_WAITTIMER            = 11,
	VS_HIB_EXIT_CONF            = 12,
	VS_HIB_EXIT                 = 13,
	VS_DS_IDLEEN                = 14,
	VS_DS_ENTER                 = 15,
	VS_DS_CONF                  = 16,
	VS_DS_MIDDLE                = 17,
	VS_XOUFS_CKOFF              = 18,
	VS_XOUFS_MID                = 19,
	VS_XOUFS_CKON               = 20,
	VS_HCE_PRE_RESET            = 21
};
