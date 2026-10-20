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

#include "ufs_sys.h"

/* DMA related configurations */

/*
 * Maximum data length in one command.
 *
 * It can be changed according to scenario but notice that RAM usage
 * will also be increased (for PRDT table) with larger data length.
 */
#define UFS_MAX_DATA_SIZE_PER_CMD_MB (32)
#define UFS_MAX_SIZE_PER_SG_SEGMENT  (256 * 1024)    // UFSHCI 2.1 spec: 256 KB
#define UFS_MAX_SG_SEGMENTS \
    (UFS_MAX_DATA_SIZE_PER_CMD_MB * 1024 * 1024 / UFS_MAX_SIZE_PER_SG_SEGMENT)

enum {
    TASK_REQ_UPIU_SIZE_DWORDS   = 8,
    TASK_RSP_UPIU_SIZE_DWORDS   = 8,
    ALIGNED_UPIU_SIZE           = 512,
};

/* UFSHCI Registers */
enum {
    REG_CONTROLLER_CAPABILITIES             = 0x00,
    REG_UFS_VERSION                         = 0x08,
    REG_CONTROLLER_DEV_ID                   = 0x10,
    REG_CONTROLLER_PROD_ID                  = 0x14,
    REG_AHIT                                = 0x18,
    REG_INTERRUPT_STATUS                    = 0x20,
    REG_INTERRUPT_ENABLE                    = 0x24,
    REG_CONTROLLER_STATUS                   = 0x30,
    REG_CONTROLLER_ENABLE                   = 0x34,
    REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER    = 0x38,
    REG_UIC_ERROR_CODE_DATA_LINK_LAYER      = 0x3C,
    REG_UIC_ERROR_CODE_NETWORK_LAYER        = 0x40,
    REG_UIC_ERROR_CODE_TRANSPORT_LAYER      = 0x44,
    REG_UIC_ERROR_CODE_DME                  = 0x48,
    REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL    = 0x4C,
    REG_UTP_TRANSFER_REQ_LIST_BASE_L        = 0x50,
    REG_UTP_TRANSFER_REQ_LIST_BASE_H        = 0x54,
    REG_UTP_TRANSFER_REQ_DOOR_BELL          = 0x58,
    REG_UTP_TRANSFER_REQ_LIST_CLEAR         = 0x5C,
    REG_UTP_TRANSFER_REQ_LIST_RUN_STOP      = 0x60,
    REG_UTP_TRANSFER_REQ_LIST_COMP_NOTIFY   = 0x64,
    REG_UTP_TASK_REQ_LIST_BASE_L            = 0x70,
    REG_UTP_TASK_REQ_LIST_BASE_H            = 0x74,
    REG_UTP_TASK_REQ_DOOR_BELL              = 0x78,
    REG_UTP_TASK_REQ_LIST_CLEAR             = 0x7C,
    REG_UTP_TASK_REQ_LIST_RUN_STOP          = 0x80,
    REG_UIC_COMMAND                         = 0x90,
    REG_UIC_COMMAND_ARG_1                   = 0x94,
    REG_UIC_COMMAND_ARG_2                   = 0x98,
    REG_UIC_COMMAND_ARG_3                   = 0x9C,
	REG_SPACE_SIZE                          = 0xA0,
    REG_UMABA                               = 0xB0,
    REG_UMABAU                              = 0xB4,
    REG_UMAOMAX                             = 0xB8,
    REG_UMACONF                             = 0xBC,
    REG_XCNF                                = 0xC8,
    REG_CDACFG                              = 0xD0,
    REG_CDATX1                              = 0xD4,
    REG_CDATX2                              = 0xD8,
    REG_CDARX1                              = 0xDC,
    REG_CDARX2                              = 0xE0,
    REG_CDASTA                              = 0xE4,
    REG_CRYPTO_CAPABILITY                   = 0x100,
    REG_CRYPTO_CAPABILITY_X                 = 0x104,
	REG_UFS_XOUFS_CTRL                      = 0x140,
	REG_UFS_REFCLK_CTRL                     = 0x144,
	REG_UFS_UFS_MMIO_OTSD_CTRL              = 0x14C,
	REG_UFS_MMIO_OPT_CTRL_0                 = 0x160,
	REG_UFS_MMIO_STS_0                      = 0x170,
	/* AHB DBG Monitor */
	REG_UFS_MMIO_DBG_AHB                    = 0x1C0,
	REG_UFS_MMIO_DBG_AHB_HADDR              = 0x1C4,
	REG_UFS_MMIO_DBG_AHB_HRDATA             = 0x1C8,
	REG_UFS_MMIO_DBG_AHB_HWDATA             = 0x1CC,
	REG_UFS_MMIO_DBG_NIT                    = 0x1D0,
	REG_UFS_MMIO_DBG_NIT_RDATA              = 0x1D4,
	REG_UFS_MMIO_DBG_NIT_WDATA              = 0x1D8,
	/* AXI DBG Monitor */
	REG_UFS_MMIO_DBG_AXIM                   = 0x1FC,
	REG_UFS_MEM_CFG                         = 0x300,
	REG_UFS_MCQ_CFG                         = 0x380,
	REG_UFS_EXTREG                          = 0x2100,
	REG_UFS_MPHYCTRL                        = 0x2200,
	REG_MTK_AUTO_DEEP_STALL                 = 0x2210,
	REG_UFS_FPGA_I2C_OUT                    = 0x2220,
	REG_UFS_FPGA_I2C_IN                     = 0x2224,
	REG_UFS_MTK_IP_VER                      = 0x2240,
	REG_UFS_MTK_OCS_ERR_STATUS              = 0x2244,
	REG_UFS_MTK_STATUS                      = 0x2248,
	/* UPIU Monitor */
	REG_UFS_NOPOUT_MON                      = 0x2280,
	REG_UFS_NOPIN_MON                       = 0x2284,
	REG_UFS_COMMAND_MON                     = 0x2288,
	REG_UFS_RESP_MON                        = 0x228C,
	REG_UFS_DATAOUT_MON                     = 0x2290,
	REG_UFS_DATAIN_MON                      = 0x2294,
	REG_UFS_TMREQ_MON                       = 0x2298,
	REG_UFS_TMRESP_MON                      = 0x229C,
	REG_UFS_RTT_MON                         = 0x22A0,
	REG_UFS_QUERYREQ_MON                    = 0x22A4,
	REG_UFS_QUERYRESP_MON                   = 0x22A8,
	REG_UFS_REJECT_MON                      = 0x22AC,
	REG_UFS_AH8E_MON                        = 0x22B0,
	REG_UFS_AH8X_MON                        = 0x22B4,
	/* Debug Signal */
	REG_UFS_DEBUG_SEL                       = 0x22C0,
	REG_UFS_SW_DBG                          = 0x22C4,
	REG_UFS_PROBE                           = 0x22C8,
	REG_UFS_AES_DEBUG_SEL                   = 0x22CC,
	REG_UFS_DEBUG_SEL_B0                    = 0x22D0,
	REG_UFS_DEBUG_SEL_B1                    = 0x22D4,
	REG_UFS_DEBUG_SEL_B2                    = 0x22D8,
	REG_UFS_DEBUG_SEL_B3                    = 0x22DC,
	REG_UFS_MDBG_SEL_EXT_B0                 = 0x22E0,
	REG_UFS_MDBG_SEL_EXT_B1                 = 0x22E4,
	REG_UFS_MDBG_SEL_EXT_B2                 = 0x22E8,
	REG_UFS_MDBG_SEL_EXT_B3                 = 0x22EC,
	REG_UFS_DEBUG_MSEL_G0                   = 0x22F0,
	REG_UFS_DEBUG_MSEL_G1                   = 0x22F4,
	REG_UFS_DEBUG_MSEL_G2                   = 0x22F8,
	REG_UFS_SQ_DBR_DBG                      = 0x22FC,
	REG_UFS_ACT_STS                         = 0x2304
};

#define UTP_TRANSFER_REQ_COMPL      UFS_BIT(0)
#define UIC_DME_END_PT_RESET        UFS_BIT(1)
#define UIC_ERROR                   UFS_BIT(2)
#define UIC_TEST_MODE               UFS_BIT(3)
#define UIC_POWER_MODE              UFS_BIT(4)
#define UIC_HIBERNATE_EXIT          UFS_BIT(5) 
#define UIC_HIBERNATE_ENTER         UFS_BIT(6)
#define UIC_LINK_LOST               UFS_BIT(7)
#define UIC_LINK_STARTUP            UFS_BIT(8)
#define UTP_TASK_REQ_COMPL          UFS_BIT(9)
#define UIC_COMMAND_COMPL           UFS_BIT(10)
#define DEVICE_FATAL_ERROR          UFS_BIT(11)
#define CONTROLLER_FATAL_ERROR      UFS_BIT(16)
#define SYSTEM_BUS_FATAL_ERROR      UFS_BIT(17)

#define UFSHCD_UIC_PWR_MASK (UIC_HIBERNATE_ENTER |\
                UIC_HIBERNATE_EXIT |\
                UIC_POWER_MODE)

#define UFSHCD_UIC_MASK     (UIC_COMMAND_COMPL | UFSHCD_UIC_PWR_MASK)

#define UFSHCD_UIC_HIBERN8_MASK	(UIC_HIBERNATE_ENTER |\
				UIC_HIBERNATE_EXIT)

#define UFSHCD_ERROR_MASK   (UIC_ERROR |\
                DEVICE_FATAL_ERROR |\
                CONTROLLER_FATAL_ERROR |\
                SYSTEM_BUS_FATAL_ERROR)

#define INT_FATAL_ERRORS    (DEVICE_FATAL_ERROR |\
                CONTROLLER_FATAL_ERROR |\
                SYSTEM_BUS_FATAL_ERROR)
/* UFS Version 08h */
#define MINOR_VERSION_NUM_MASK	UFS_MASK(0xFFFF, 0)
#define MAJOR_VERSION_NUM_MASK	UFS_MASK(0xFFFF, 16)

/* Controller UFSHCI version */
enum {
	UFSHCI_VERSION_10 = 0x00010000, /* 1.0 */
	UFSHCI_VERSION_11 = 0x00010100, /* 1.1 */
	UFSHCI_VERSION_20 = 0x00000200, /* 2.0 */
	UFSHCI_VERSION_21 = 0x00000210, /* 2.1 */
	UFSHCI_VERSION_40 = 0x00000400, /* 4.0 */
	UFSHCI_VERSION_41 = 0x00000410, /* 4.1 */
	UFSHCI_VERSION_50 = 0x00000500, /* 5.0 */
};

/* HCS - Host Controller Status 30h */
#define DEVICE_PRESENT                          UFS_BIT(0)
#define UTP_TRANSFER_REQ_LIST_READY             UFS_BIT(1)
#define UTP_TASK_REQ_LIST_READY                 UFS_BIT(2)
#define UIC_COMMAND_READY                       UFS_BIT(3)
#define HOST_ERROR_INDICATOR                    UFS_BIT(4)
#define DEVICE_ERROR_INDICATOR                  UFS_BIT(5)
#define UIC_POWER_MODE_CHANGE_REQ_STATUS_MASK   UFS_MASK(0x7, 8)

enum {
    PWR_OK          = 0x0,
    PWR_LOCAL       = 0x01,
    PWR_REMOTE      = 0x02,
    PWR_BUSY        = 0x03,
    PWR_ERROR_CAP   = 0x04,
    PWR_FATAL_ERROR = 0x05,
};

/* HCE - Host Controller Enable 34h */
#define CONTROLLER_ENABLE   UFS_BIT(0)
#define CONTROLLER_DISABLE  0x0
#define CRYPTO_GENERAL_ENABLE	UFS_BIT(1)

/* UECPA - Host UIC Error Code PHY Adapter Layer 38h */
#define UIC_PHY_ADAPTER_LAYER_ERROR                 UFS_BIT(31)
#define UIC_PHY_ADAPTER_LAYER_ERROR_CODE_MASK       0x1F
#define UIC_PHY_ADAPTER_LAYER_LANE_ERR_MASK		    0xF
#define UIC_PHY_ADAPTER_LAYER_GENERIC_ERROR		    0x10

/* UECDL - Host UIC Error Code Data Link Layer 3Ch */
#define UIC_DATA_LINK_LAYER_ERROR           UFS_BIT(31)
#define UIC_DATA_LINK_LAYER_ERROR_CODE_MASK 0x7FFF
#define UIC_DATA_LINK_LAYER_ERROR_TCX_REP_TIMER_EXP	0x2
#define UIC_DATA_LINK_LAYER_ERROR_AFCX_REQ_TIMER_EXP	0x4
#define UIC_DATA_LINK_LAYER_ERROR_FCX_PRO_TIMER_EXP	0x8
#define UIC_DATA_LINK_LAYER_ERROR_RX_BUF_OF	0x20
#define UIC_DATA_LINK_LAYER_ERROR_PA_INIT	0x2000
#define UIC_DATA_LINK_LAYER_ERROR_NAC_RECEIVED	0x0001
#define UIC_DATA_LINK_LAYER_ERROR_TCx_REPLAY_TIMEOUT 0x0002

/* UECN - Host UIC Error Code Network Layer 40h */
#define UIC_NETWORK_LAYER_ERROR             UFS_BIT(31)
#define UIC_NETWORK_LAYER_ERROR_CODE_MASK   0x7
#define UIC_NETWORK_UNSUPPORTED_HEADER_TYPE	0x1
#define UIC_NETWORK_BAD_DEVICEID_ENC		0x2
#define UIC_NETWORK_LHDR_TRAP_PACKET_DROPPING	0x4

/* UECT - Host UIC Error Code Transport Layer 44h */
#define UIC_TRANSPORT_LAYER_ERROR           UFS_BIT(31)
#define UIC_TRANSPORT_LAYER_ERROR_CODE_MASK 0x7F
#define UIC_TRANSPORT_UNSUPPORTED_HEADER_TYPE	0x1
#define UIC_TRANSPORT_UNKNOWN_CPORTID		0x2
#define UIC_TRANSPORT_NO_CONNECTION_RX		0x4
#define UIC_TRANSPORT_CONTROLLED_SEGMENT_DROPPING	0x8
#define UIC_TRANSPORT_BAD_TC			0x10
#define UIC_TRANSPORT_E2E_CREDIT_OVERFOW	0x20
#define UIC_TRANSPORT_SAFETY_VALUE_DROPPING	0x40

/* UECDME - Host UIC Error Code DME 48h */
#define UIC_DME_ERROR               UFS_BIT(31)
#define UIC_DME_ERROR_CODE_MASK     0x1

#define INT_AGGR_TIMEOUT_VAL_MASK           0xFF
#define INT_AGGR_COUNTER_THRESHOLD_MASK     UFS_MASK(0x1F, 8)
#define INT_AGGR_COUNTER_AND_TIMER_RESET    UFS_BIT(16)
#define INT_AGGR_STATUS_BIT                 UFS_BIT(20)
#define INT_AGGR_PARAM_WRITE                UFS_BIT(24)
#define INT_AGGR_ENABLE                     UFS_BIT(31)

/* UTRLRSR - UTP Transfer Request Run-Stop Register 60h */
#define UTP_TRANSFER_REQ_LIST_RUN_STOP_BIT  UFS_BIT(0)

/* UTMRLRSR - UTP Task Management Request Run-Stop Register 80h */
#define UTP_TASK_REQ_LIST_RUN_STOP_BIT      UFS_BIT(0)

/*
 * Ref-clk control 144h
 *
 * Values for register REG_UFS_REFCLK_CTRL
 */
#define REFCLK_RELEASE              0x0
#define REFCLK_REQUEST              UFS_BIT(0)
#define REFCLK_ACK                  UFS_BIT(1)

/* UICCMD - UIC Command */
#define COMMAND_OPCODE_MASK         0xFF
#define GEN_SELECTOR_INDEX_MASK     0xFFFF

/* CDACFG */
#define CDARXALIGN (0x1<<29)
#define CDAEN      (0x1<<28)
#define CDAEOM     (0x1<<16)

/* CDASTA */
#define FLAG_CDARES  (0x3 << 20)
#define FLAG_CDABUSY (0x1 << 19)
#define FLAG_CDASTA  (0x1 << 18)

#define MIB_ATTRIBUTE_MASK          UFS_MASK(0xFFFF, 16)
#define RESET_LEVEL                 0xFF

#define ATTR_SET_TYPE_MASK          UFS_MASK(0xFF, 16)
#define CONFIG_RESULT_CODE_MASK     0xFF
#define GENERIC_ERROR_CODE_MASK     0xFF

#define UIC_ARG_MIB_SEL(attr, sel)  ((((unsigned)(attr) & 0xFFFF) << 16) |\
                     ((sel) & 0xFFFF))
#define UIC_ARG_MIB(attr)       UIC_ARG_MIB_SEL(attr, 0)
#define UIC_ARG_ATTR_TYPE(t)        (((t) & 0xFF) << 16)
#define UIC_GET_ATTR_ID(v)      (((v) >> 16) & 0xFFFF)

/* UIC Commands */
enum uic_cmd_dme {
    UIC_CMD_DME_GET             = 0x01,
    UIC_CMD_DME_SET             = 0x02,
    UIC_CMD_DME_PEER_GET        = 0x03,
    UIC_CMD_DME_PEER_SET        = 0x04,
    UIC_CMD_DME_POWERON         = 0x10,
    UIC_CMD_DME_POWEROFF        = 0x11,
    UIC_CMD_DME_ENABLE          = 0x12,
    UIC_CMD_DME_RESET           = 0x14,
    UIC_CMD_DME_END_PT_RST      = 0x15,
    UIC_CMD_DME_LINK_STARTUP    = 0x16,
    UIC_CMD_DME_HIBER_ENTER     = 0x17,
    UIC_CMD_DME_HIBER_EXIT      = 0x18,
    UIC_CMD_DME_TEST_MODE       = 0x1A,
};

/* UIC Config result code / Generic error code */
enum {
    UIC_CMD_RESULT_SUCCESS                  = 0x00,
    UIC_CMD_RESULT_INVALID_ATTR             = 0x01,
    UIC_CMD_RESULT_FAILURE                  = 0x01,
    UIC_CMD_RESULT_INVALID_ATTR_VALUE       = 0x02,
    UIC_CMD_RESULT_READ_ONLY_ATTR           = 0x03,
    UIC_CMD_RESULT_WRITE_ONLY_ATTR          = 0x04,
    UIC_CMD_RESULT_BAD_INDEX                = 0x05,
    UIC_CMD_RESULT_LOCKED_ATTR              = 0x06,
    UIC_CMD_RESULT_BAD_TEST_FEATURE_INDEX   = 0x07,
    UIC_CMD_RESULT_PEER_COMM_FAILURE        = 0x08,
    UIC_CMD_RESULT_BUSY                     = 0x09,
    UIC_CMD_RESULT_DME_FAILURE              = 0x0A,
};

enum ufs_event_type {
	/* uic specific errors */
	UFS_EVT_PA_ERR = 0,
	UFS_EVT_DL_ERR,
	UFS_EVT_NL_ERR,
	UFS_EVT_TL_ERR,
	UFS_EVT_DME_ERR,

	/* fatal errors */
	UFS_EVT_AUTO_HIBERN8_ERR,
	UFS_EVT_FATAL_ERR,
	UFS_EVT_LINK_STARTUP_FAIL,
	UFS_EVT_RESUME_ERR,
	UFS_EVT_SUSPEND_ERR,
	UFS_EVT_WL_SUSP_ERR,
	UFS_EVT_WL_RES_ERR,

	/* abnormal events */
	UFS_EVT_DEV_RESET,
	UFS_EVT_HOST_RESET,
	UFS_EVT_ABORT,

	UFS_EVT_CNT,
};

#define MASK_UIC_COMMAND_RESULT         0xFF

#define INT_AGGR_COUNTER_THLD_VAL(c)    (((c) & 0x1F) << 8)
#define INT_AGGR_TIMEOUT_VAL(t)         (((t) & 0xFF) << 0)

/* Interrupt disable masks */
enum {
    /* Interrupt disable mask for UFSHCI v1.0 */
    INTERRUPT_MASK_ALL_VER_10   = 0x30FFF,
    INTERRUPT_MASK_RW_VER_10    = 0x30000,

    /* Interrupt disable mask for UFSHCI v1.1 */
    INTERRUPT_MASK_ALL_VER_11   = 0x31FFF,
};

/* CCAP - Crypto Capability 100h */
union ufs_crypto_capabilities {
	__le32 reg_val;
	struct {
		u8 num_crypto_cap;
		u8 config_count;
		u8 reserved;
		u8 config_array_ptr;
	};
};

enum ufs_crypto_key_size {
	UFS_CRYPTO_KEY_SIZE_INVALID	= 0x0,
	UFS_CRYPTO_KEY_SIZE_128		= 0x1,
	UFS_CRYPTO_KEY_SIZE_192		= 0x2,
	UFS_CRYPTO_KEY_SIZE_256		= 0x3,
	UFS_CRYPTO_KEY_SIZE_512		= 0x4,
};

enum ufs_crypto_alg {
	UFS_CRYPTO_ALG_AES_XTS			= 0x0,
	UFS_CRYPTO_ALG_BITLOCKER_AES_CBC	= 0x1,
	UFS_CRYPTO_ALG_AES_ECB			= 0x2,
	UFS_CRYPTO_ALG_ESSIV_AES_CBC		= 0x3,
};

/* x-CRYPTOCAP - Crypto Capability X */
union ufs_crypto_cap_entry {
	__le32 reg_val;
	struct {
		u8 algorithm_id;
		u8 sdus_mask; /* Supported data unit size mask */
		u8 key_size;
		u8 reserved;
	};
};

#define UFS_CRYPTO_CONFIGURATION_ENABLE (1 << 7)
#define UFS_CRYPTO_KEY_MAX_SIZE 64
/* x-CRYPTOCFG - Crypto Configuration X */
union ufs_crypto_cfg_entry {
	__le32 reg_val[32];
	struct {
		u8 crypto_key[UFS_CRYPTO_KEY_MAX_SIZE];
		u8 data_unit_size;
		u8 crypto_cap_idx;
		u8 reserved_1;
		u8 config_enable;
		u8 reserved_multi_host;
		u8 reserved_2;
		u8 vsb[2];
		u8 reserved_3[56];
	};
};

/*
 * Request Descriptor Definitions
 */

/* Transfer request command type */
enum {
    UTP_CMD_TYPE_SCSI           = 0x0,
    UTP_CMD_TYPE_UFS            = 0x1,
    UTP_CMD_TYPE_DEV_MANAGE     = 0x2,
};

enum {
    UTP_SCSI_COMMAND                = 0x00000000,
    UTP_NATIVE_UFS_COMMAND          = 0x10000000,
    UTP_DEVICE_MANAGEMENT_FUNCTION  = 0x20000000,
    UTP_REQ_DESC_INT_CMD            = 0x01000000,
};

/* UTP Transfer Request Data Direction (DD) */
enum {
    UTP_NO_DATA_TRANSFER    = 0x00000000,
    UTP_HOST_TO_DEVICE      = 0x02000000,
    UTP_DEVICE_TO_HOST      = 0x04000000,
};

/* Overall command status values */
enum utp_ocs {
    OCS_SUCCESS                 = 0x0,
    OCS_INVALID_CMD_TABLE_ATTR  = 0x1,
    OCS_INVALID_PRDT_ATTR       = 0x2,
    OCS_MISMATCH_DATA_BUF_SIZE  = 0x3,
    OCS_MISMATCH_RESP_UPIU_SIZE = 0x4,
    OCS_PEER_COMM_FAILURE       = 0x5,
    OCS_ABORTED                 = 0x6,
    OCS_FATAL_ERROR             = 0x7,
    OCS_DEVICE_FATAL_ERROR		= 0x8,
    OCS_INVALID_CRYPTO_CONFIG	= 0x9,
    OCS_GENERAL_CRYPTO_ERROR	= 0xA,
    OCS_INVALID_COMMAND_STATUS  = 0x0F,
    MASK_OCS                    = 0x0F,
};

/* The maximum length of the data byte count field in the PRDT is 256KB */
#define PRDT_DATA_BYTE_COUNT_MAX    (256 * 1024)
/* The granularity of the data byte count field in the PRDT is 32-bit */
#define PRDT_DATA_BYTE_COUNT_PAD    4

enum ufs_ref_clk_freq {
	REF_CLK_FREQ_19_2_MHZ	= 0,
	REF_CLK_FREQ_26_MHZ	= 1,
	REF_CLK_FREQ_38_4_MHZ	= 2,
	REF_CLK_FREQ_52_MHZ	= 3,
	REF_CLK_FREQ_INVAL	= -1,
};

/**
 * struct ufs_sg_entry - UFSHCI PRD Entry
 * @base_addr: Lower 32bit physical address DW-0
 * @upper_addr: Upper 32bit physical address DW-1
 * @reserved: Reserved for future use DW-2
 * @size: size of physical segment DW-3
 */
struct ufs_sg_entry {
    __le32    base_addr;
    __le32    upper_addr;
    __le32    reserved;
    __le32    size;
};

/**
 * struct utp_transfer_cmd_desc - UFS Command Descriptor structure
 * @command_upiu: Command UPIU Frame address
 * @response_upiu: Response UPIU Frame address
 * @prd_table: Physical Region Descriptor
 */
struct utp_transfer_cmd_desc {
    u8 command_upiu[ALIGNED_UPIU_SIZE];
    u8 response_upiu[ALIGNED_UPIU_SIZE];
    struct ufs_sg_entry    prd_table[UFS_MAX_SG_SEGMENTS];
};

/**
 * struct request_desc_header - Descriptor Header common to both UTRD and UTMRD
 * @dword0: Descriptor Header DW0
 * @dword1: Descriptor Header DW1
 * @dword2: Descriptor Header DW2
 * @dword3: Descriptor Header DW3
 */
struct request_desc_header {
    __le32 dword_0;
    __le32 dword_1;
    __le32 dword_2;
    __le32 dword_3;
};

/**
 * struct utp_transfer_req_desc - UTRD structure
 * @header: UTRD header DW-0 to DW-3
 * @command_desc_base_addr_lo: UCD base address low DW-4
 * @command_desc_base_addr_hi: UCD base address high DW-5
 * @response_upiu_length: response UPIU length DW-6
 * @response_upiu_offset: response UPIU offset DW-6
 * @prd_table_length: Physical region descriptor length DW-7
 * @prd_table_offset: Physical region descriptor offset DW-7
 */
struct utp_transfer_req_desc {

    /* DW 0-3 */
    struct request_desc_header header;

    /* DW 4-5*/
    __le32  command_desc_base_addr_lo;
    __le32  command_desc_base_addr_hi;

    /* DW 6 */
    __le16  response_upiu_length;
    __le16  response_upiu_offset;

    /* DW 7 */
    __le16  prd_table_length;
    __le16  prd_table_offset;
};

/**
 * struct utp_task_req_desc - UTMRD structure
 * @header: UTMRD header DW-0 to DW-3
 * @task_req_upiu: Pointer to task request UPIU DW-4 to DW-11
 * @task_rsp_upiu: Pointer to task response UPIU DW12 to DW-19
 */
struct utp_task_req_desc {

    /* DW 0-3 */
    struct request_desc_header header;

    /* DW 4-11 */
    __le32 task_req_upiu[TASK_REQ_UPIU_SIZE_DWORDS];

    /* DW 12-19 */
    __le32 task_rsp_upiu[TASK_RSP_UPIU_SIZE_DWORDS];
};

