/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include "ufs_sys.h"

#define UFS_SECURITY_PROTOCOL (0xec)
#define RPMB_PROTOCOL_ID 0x1
#define RPMB_FRM_STUFF_LEN 196
#define RPMB_FRM_KEY_MAC_LEN 32
#define RPMB_FRM_DATA_LEN 256
#define RPMB_FRM_NONCE_LEN 16
#define RPMB_FRM_WR_CNT_LEN 4
#define RPMB_FRM_ADDR_LEN 2
#define RPMB_FRM_BLK_CNT_LEN 2
#define RPMB_FRM_RESULT_LEN 2
#define RPMB_FRM_REQ_RESP_LEN 2
#define RPMB_SWPCB_VALID_DATA_SIZE 80
#define HMAC_TEXT_LEN 284

#define RPMB_REGION_0 0x00
#define RPMB_REGION_1 0x01
#define RPMB_REGION_2 0x02
#define RPMB_REGION_3 0x03

static const char rpmb_oper_result[][64] = {
    "Operation OK",
    "General failure",
    "Authentication failure(MAC not match)",
    "Counter failure(Counter not match or Inc fail)",
    "Address failure (out of range or wrong alignment)",
    "Write failure (data/counter/result write failure)",
    "Read failure (data/counter/result read failure)",
    "Authentication Key not yet programmed",
    "Secure Write Protect Configuration Block access failure",
    "Invalid Secure Write Protect Block Configuration parameter",
    "Secure Write Protection not applicable",
};

struct rpmb_data_frame {
    u8 stuff[RPMB_FRM_STUFF_LEN];
    u8 key_MAC[RPMB_FRM_KEY_MAC_LEN];
    u8 data[RPMB_FRM_DATA_LEN];
    u8 nonce[RPMB_FRM_NONCE_LEN];
    u32 wr_cnt;
    u16 addr;
    u16 blk_cnt;
    u16 result;
    u16 req_resp;
} __attribute__((packed));

enum enum_rpmb_msg {
    RPMB_REQ_AUTHEN_KEY_PRG = 0x0001,
    RPMB_REQ_READ_WR_CNT = 0x0002,
    RPMB_REQ_AUTHEN_WRITE = 0x0003,
    RPMB_REQ_AUTHEN_READ = 0x0004,
    RPMB_REQ_RESULT_RD = 0x0005,
    RPMB_REQ_SEC_WPCB_WRITE = 0x0006,
    RPMB_REQ_SEC_WPCB_READ = 0x0007,

    RPMB_RSP_AUTHEN_KEY_PRG = 0x0100,
    RPMB_RSP_READ_WR_CNT = 0x0200,
    RPMB_RSP_AUTHEN_WRITE = 0x0300,
    RPMB_RSP_AUTHEN_READ = 0x0400,
    RPMB_RSP_SEC_WPCB_WRITE = 0x0600,
    RPMB_RSP_SEC_WPCB_READ = 0x0700,
};

int init_ufs_rpmb_sharemem(void);
int deinit_ufs_rpmb_sharemem(void);
int ufs_rpmb_request(u8 region, const struct rpmb_data_frame* req_frm, u32 req_nfrm,
                     struct rpmb_data_frame* rsp_frm, u32 rsp_nfrm, int* result);
