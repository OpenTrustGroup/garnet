/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copyright (c) 2026 MediaTek Inc.
 */

#include "ufs.h"
#include "ufs_core.h"
#include "ufs_hcd.h"
#include "ufs_host.h"
#include "ufs_interface.h"
#include "ufs_rpmb.h"
#include "ufs_sip.h"
#include "ufs_utils.h"

#include <endian.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>

#define RPMB_KEY_SIZE 32
#define RPMB_MAC_SIZE 32
#define RPMB_TRANSFER_SIZE ((u32)sizeof(struct rpmb_data_frame))
#define RPMB_MAX_FRAME_COUNT 256
#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

struct sha256_ctx {
    u32 state[8];
    uint64_t total_len;
    u8 block[SHA256_BLOCK_SIZE];
    size_t block_len;
};

static const u32 k_sha256_rounds[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static const char* rpmb_msg_type_to_string(u16 msg_type) {
    switch (msg_type) {
    case RPMB_REQ_AUTHEN_KEY_PRG:
        return "key-program";
    case RPMB_REQ_READ_WR_CNT:
        return "read-counter";
    case RPMB_REQ_AUTHEN_WRITE:
        return "authenticated-write";
    case RPMB_REQ_AUTHEN_READ:
        return "authenticated-read";
    case RPMB_REQ_RESULT_RD:
        return "result-read";
    case RPMB_REQ_SEC_WPCB_WRITE:
        return "sec-wpcb-write";
    case RPMB_REQ_SEC_WPCB_READ:
        return "sec-wpcb-read";
    case RPMB_RSP_AUTHEN_KEY_PRG:
        return "key-program-rsp";
    case RPMB_RSP_READ_WR_CNT:
        return "read-counter-rsp";
    case RPMB_RSP_AUTHEN_WRITE:
        return "authenticated-write-rsp";
    case RPMB_RSP_AUTHEN_READ:
        return "authenticated-read-rsp";
    case RPMB_RSP_SEC_WPCB_WRITE:
        return "sec-wpcb-write-rsp";
    case RPMB_RSP_SEC_WPCB_READ:
        return "sec-wpcb-read-rsp";
    default:
        return "unknown";
    }
}

static inline u32 sha256_rotr(u32 value, u32 shift) {
    return (value >> shift) | (value << (32 - shift));
}

static inline u32 sha256_ch(u32 x, u32 y, u32 z) {
    return (x & y) ^ (~x & z);
}

static inline u32 sha256_maj(u32 x, u32 y, u32 z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline u32 sha256_big_sigma0(u32 x) {
    return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22);
}

static inline u32 sha256_big_sigma1(u32 x) {
    return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25);
}

static inline u32 sha256_small_sigma0(u32 x) {
    return sha256_rotr(x, 7) ^ sha256_rotr(x, 18) ^ (x >> 3);
}

static inline u32 sha256_small_sigma1(u32 x) {
    return sha256_rotr(x, 17) ^ sha256_rotr(x, 19) ^ (x >> 10);
}

static void sha256_transform(struct sha256_ctx* ctx, const u8 block[SHA256_BLOCK_SIZE]) {
    u32 w[64];
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 e;
    u32 f;
    u32 g;
    u32 h;
    size_t i;

    for (i = 0; i < 16; ++i) {
        w[i] = ((u32)block[i * 4] << 24) |
               ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) |
               (u32)block[i * 4 + 3];
    }

    for (i = 16; i < 64; ++i) {
        w[i] = sha256_small_sigma1(w[i - 2]) + w[i - 7] +
               sha256_small_sigma0(w[i - 15]) + w[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        u32 t1 = h + sha256_big_sigma1(e) + sha256_ch(e, f, g) +
                 k_sha256_rounds[i] + w[i];
        u32 t2 = sha256_big_sigma0(a) + sha256_maj(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->total_len = 0;
    ctx->block_len = 0;
}

static void sha256_update(struct sha256_ctx* ctx, const u8* data, size_t len) {
    while (len != 0) {
        size_t copy_len = SHA256_BLOCK_SIZE - ctx->block_len;

        if (copy_len > len) {
            copy_len = len;
        }

        memcpy(ctx->block + ctx->block_len, data, copy_len);
        ctx->block_len += copy_len;
        ctx->total_len += copy_len;
        data += copy_len;
        len -= copy_len;

        if (ctx->block_len == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx* ctx, u8 out[SHA256_DIGEST_SIZE]) {
    uint64_t bit_len = ctx->total_len * 8;
    size_t i;

    ctx->block[ctx->block_len++] = 0x80;

    if (ctx->block_len > 56) {
        while (ctx->block_len < SHA256_BLOCK_SIZE) {
            ctx->block[ctx->block_len++] = 0;
        }
        sha256_transform(ctx, ctx->block);
        ctx->block_len = 0;
    }

    while (ctx->block_len < 56) {
        ctx->block[ctx->block_len++] = 0;
    }

    for (i = 0; i < 8; ++i) {
        ctx->block[56 + i] = (u8)(bit_len >> (56 - i * 8));
    }

    sha256_transform(ctx, ctx->block);

    for (i = 0; i < 8; ++i) {
        out[i * 4] = (u8)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (u8)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (u8)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (u8)ctx->state[i];
    }
}

static void hmac_sha256(const u8* key, size_t key_len, const u8* data, size_t data_len,
                        u8 out[SHA256_DIGEST_SIZE]) {
    struct sha256_ctx ctx;
    u8 key_block[SHA256_BLOCK_SIZE];
    u8 inner_hash[SHA256_DIGEST_SIZE];
    size_t i;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    for (i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        key_block[i] ^= 0x36;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, key_block, sizeof(key_block));
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    for (i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        key_block[i] ^= (0x36 ^ 0x5c);
    }
    sha256_init(&ctx);
    sha256_update(&ctx, key_block, sizeof(key_block));
    sha256_update(&ctx, inner_hash, sizeof(inner_hash));
    sha256_final(&ctx, out);

    memset(key_block, 0, sizeof(key_block));
    memset(inner_hash, 0, sizeof(inner_hash));
}

static void rpmb_req_copy_data_for_hmac(u8* buf, const struct rpmb_data_frame* frame) {
    memcpy(buf, frame->data, RPMB_FRM_DATA_LEN);
    buf += RPMB_FRM_DATA_LEN;

    memcpy(buf, frame->nonce, sizeof(frame->nonce));
    buf += sizeof(frame->nonce);

    memcpy(buf, &frame->wr_cnt, sizeof(frame->wr_cnt));
    buf += sizeof(frame->wr_cnt);

    memcpy(buf, &frame->addr, sizeof(frame->addr));
    buf += sizeof(frame->addr);

    memcpy(buf, &frame->blk_cnt, sizeof(frame->blk_cnt));
    buf += sizeof(frame->blk_cnt);

    memcpy(buf, &frame->result, sizeof(frame->result));
    buf += sizeof(frame->result);

    memcpy(buf, &frame->req_resp, sizeof(frame->req_resp));
}

static int rpmb_fill_nonce(u8* nonce, size_t len) {
    size_t actual = 0;
    zx_status_t status = zx_cprng_draw(nonce, len, &actual);

    if (status != ZX_OK || actual != len) {
        UFS_LOGE("failed to generate nonce: status=%d actual=%zu len=%zu\n", status, actual,
                 len);
        return -UFS_RPMB_DRIVER_ERROR;
    }

    return 0;
}

static int rpmb_smc_call(uint64_t arg1, uint64_t arg2, uint64_t arg3, zx_smc_result_t* result) {
    zx_smc_parameters_t params = {
        .func_id = MTK_SIP_UFS_CONTROL,
        .arg1 = arg1,
        .arg2 = arg2,
        .arg3 = arg3,
        .arg4 = 0,
        .arg5 = 0,
        .arg6 = 0,
        .client_id = 0,
        .secure_os_id = 0,
    };
    zx_status_t status = zx_smc_call(get_root_resource(), &params, result);

    if (status != ZX_OK) {
        UFS_LOGE("smc call failed: status=%d arg1=%ld arg2=%ld arg3=%ld\n", status,
                 arg1, arg2, arg3);
        return -UFS_RPMB_DRIVER_ERROR;
    }

    return 0;
}

static int rpmb_get_key(u8 region, u8* key, size_t key_len) {
    size_t offset = 0;
    size_t word_size = sizeof(unsigned long);
    size_t batch_size = word_size * 2;

    memset(key, 0, key_len);

    while (offset < key_len) {
        zx_smc_result_t result;
        int ret = rpmb_smc_call(UFS_MTK_SIP_RPMB_KEY, region, offset, &result);
        size_t copy_len;

        if (ret != 0) {
            return ret;
        }
        if (result.arg0 != 0) {
            UFS_LOGE("rpmb key query failed: region=%u offset=%zu ret=%ld\n", region, offset,
                     result.arg0);
            return -UFS_RPMB_DRIVER_ERROR;
        }

        copy_len = key_len - offset;
        if (copy_len > word_size) {
            copy_len = word_size;
        }
        memcpy(key + offset, &result.arg1, copy_len);
        offset += copy_len;

        if (offset >= key_len) {
            break;
        }

        copy_len = key_len - offset;
        if (copy_len > word_size) {
            copy_len = word_size;
        }
        memcpy(key + offset, &result.arg2, copy_len);
        offset += copy_len;

        if (batch_size == 0) {
            return -UFS_RPMB_DRIVER_ERROR;
        }
    }

    return 0;
}

static int rpmb_get_mac(u8 region, const struct rpmb_data_frame* frame,
                        u8 mac_out[RPMB_MAC_SIZE]) {
    u8 key[RPMB_KEY_SIZE];
    u8 data[HMAC_TEXT_LEN];
    int ret = rpmb_get_key(region, key, sizeof(key));

    if (ret != 0) {
        return ret;
    }

    rpmb_req_copy_data_for_hmac(data, frame);
    hmac_sha256(key, sizeof(key), data, sizeof(data), mac_out);

    memset(key, 0, sizeof(key));
    memset(data, 0, sizeof(data));
    return 0;
}

static int rpmb_alloc_frame_iobuf(struct ufs_hba* hba, u32 frame_count, ufs_iobuf_t* iob,
                                  struct rpmb_data_frame** frame) {
    mtk_ufs_t* ufs = hba_to_ufs(hba);
    zx_status_t status;

    if (frame_count == 0 || frame_count > RPMB_MAX_FRAME_COUNT) {
        return -UFS_RPMB_DRIVER_ERROR;
    }
    malloc(sizeof(struct ufshcd_lrb) * UFS_MAX_NUTRS);

    memset(iob, 0, sizeof(*iob));
    iob->size = frame_count * sizeof(struct rpmb_data_frame);
    iob->align = ufs->cache_line_size;
    iob->flags = IO_BUFFER_RW | IO_BUFFER_CONTIG;

    status = ufs_alloc_iobuf(ufs, iob);
    if (status != ZX_OK) {
        UFS_LOGE("failed to allocate rpmb frame buffer: %d\n", status);
        return -UFS_RPMB_DRIVER_ERROR;
    }

    *frame = (struct rpmb_data_frame*)iob->virt;
    memset(*frame, 0, iob->size);
    return 0;
}

static void rpmb_free_frame_iobuf(ufs_iobuf_t* iob) {
    ufs_free_iobuf(iob);
}

static void rpmb_prepare_security_out(struct ufs_scsi_cmd* cmd, u32 frame_count, u16 proto_spec,
                                      uintptr_t paddr) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->lun = WLUN_RPMB;
    cmd->dir = DMA_TO_DEVICE;
    cmd->exp_len = frame_count * RPMB_TRANSFER_SIZE;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 12;
    cmd->paddr = paddr;
    cmd->cmd_data[0] = SECURITY_PROTOCOL_OUT;
    cmd->cmd_data[1] = UFS_SECURITY_PROTOCOL;
    cmd->cmd_data[2] = (proto_spec >> 8) & 0xff;
    cmd->cmd_data[3] = proto_spec & 0xff;
    cmd->cmd_data[6] = (cmd->exp_len >> 24) & 0xff;
    cmd->cmd_data[7] = (cmd->exp_len >> 16) & 0xff;
    cmd->cmd_data[8] = (cmd->exp_len >> 8) & 0xff;
}

static void rpmb_prepare_security_in(struct ufs_scsi_cmd* cmd, u32 frame_count, u16 proto_spec,
                                     uintptr_t paddr) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->lun = WLUN_RPMB;
    cmd->dir = DMA_FROM_DEVICE;
    cmd->exp_len = frame_count * RPMB_TRANSFER_SIZE;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 12;
    cmd->paddr = paddr;
    cmd->cmd_data[0] = SECURITY_PROTOCOL_IN;
    cmd->cmd_data[1] = UFS_SECURITY_PROTOCOL;
    cmd->cmd_data[2] = (proto_spec >> 8) & 0xff;
    cmd->cmd_data[3] = proto_spec & 0xff;
    cmd->cmd_data[6] = (cmd->exp_len >> 24) & 0xff;
    cmd->cmd_data[7] = (cmd->exp_len >> 16) & 0xff;
    cmd->cmd_data[8] = (cmd->exp_len >> 8) & 0xff;
}

static void rpmb_prepare_sync_lrb(struct ufshcd_lrb* lrbp, uint32_t exp_len) {
    lrbp->bop = NULL;
    lrbp->iob_unmap = NULL;
    lrbp->sync_cmd = true;
    lrbp->exp_len = exp_len;
}

static int rpmb_submit_cmd(struct ufs_hba* hba, struct ufs_scsi_cmd* cmd, void* buf) {
    int tag;
    int ret;
    struct ufshcd_lrb* lrbp;

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGE("no free tag for rpmb command\n");
        return -UFS_RPMB_DRIVER_ERROR;
    }

    cmd->tag = tag;
    lrbp = &hba->lrb[tag];
    rpmb_prepare_sync_lrb(lrbp, cmd->exp_len);

    if (cmd->exp_len != 0 && buf != NULL) {
        ufs_dma_map(buf, cmd->exp_len, cmd->dir);
    }

    ret = ufshcd_queuecommand(hba, cmd);

    if (cmd->exp_len != 0 && buf != NULL) {
        ufs_dma_unmap(buf, cmd->exp_len, cmd->dir);
    }

    return ret;
}

static int rpmb_check_result(const struct rpmb_data_frame* frame, u16 expected, int* result_out) {
    u16 response = be16toh(frame->req_resp);
    u16 result = be16toh(frame->result);

    if (result_out != NULL) {
        *result_out = result;
    }

    if (response != expected) {
        UFS_LOGE("unexpected rpmb response: expected=0x%x got=0x%x\n", expected, response);
        return -UFS_RPMB_RESPONSE_ERROR;
    }

    if ((result & 0x7f) != 0) {
        u16 index = result & 0x7f;
        const char* err = "unknown";

        if (index < (sizeof(rpmb_oper_result) / sizeof(rpmb_oper_result[0]))) {
            err = rpmb_oper_result[index];
        }
        UFS_LOGE("rpmb result error: 0x%x (%s)\n", result, err);
        return -UFS_RPMB_RESULT_ERROR;
    }

    return 0;
}

static int rpmb_verify_mac(u8 region, const struct rpmb_data_frame* frame) {
    u8 mac[RPMB_MAC_SIZE];
    int ret = rpmb_get_mac(region, frame, mac);

    if (ret != 0) {
        return ret;
    }
    if (memcmp(frame->key_MAC, mac, RPMB_MAC_SIZE) != 0) {
        UFS_LOGE("rpmb mac mismatch\n");
        return -UFS_RPMB_HMAC_ERROR;
    }

    return 0;
}

static int rpmb_check_raw_response(const struct rpmb_data_frame* frame, u16 expected,
                                   int* result_out) {
    u16 response = be16toh(frame->req_resp);
    u16 result = be16toh(frame->result);

    if (result_out != NULL) {
        *result_out = result;
    }

    if (response != expected) {
        UFS_LOGE("unexpected rpmb response: expected=0x%x got=0x%x\n", expected, response);
        return -UFS_RPMB_RESPONSE_ERROR;
    }

    if ((result & 0x7f) != 0) {
        u16 index = result & 0x7f;
        const char* err = "unknown";

        if (index < (sizeof(rpmb_oper_result) / sizeof(rpmb_oper_result[0]))) {
            err = rpmb_oper_result[index];
        }
        UFS_LOGE("rpmb result error: 0x%x (%s)\n", result, err);
    }

    return result;
}

static int rpmb_security_out_locked(struct ufs_hba* hba, u8 region,
                                    const struct rpmb_data_frame* req_frm, u32 req_nfrm) {
    struct ufs_scsi_cmd cmd;
    struct rpmb_data_frame* frame;
    ufs_iobuf_t iob;
    u16 proto_spec = ((u16)region << 8) | RPMB_PROTOCOL_ID;
    int ret = rpmb_alloc_frame_iobuf(hba, req_nfrm, &iob, &frame);

    if (ret != 0) {
        return ret;
    }

    UFS_LOGI("rpmb security-out: region=%u type=0x%x(%s) frames=%u\n", region,
             be16toh(req_frm[0].req_resp), rpmb_msg_type_to_string(be16toh(req_frm[0].req_resp)),
             req_nfrm);
    memcpy(frame, req_frm, req_nfrm * sizeof(*frame));
    rpmb_prepare_security_out(&cmd, req_nfrm, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to send rpmb security out request: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
    }

    rpmb_free_frame_iobuf(&iob);
    return ret;
}

static int rpmb_security_in_locked(struct ufs_hba* hba, u8 region, struct rpmb_data_frame* rsp_frm,
                                   u32 rsp_nfrm) {
    struct ufs_scsi_cmd cmd;
    struct rpmb_data_frame* frame;
    ufs_iobuf_t iob;
    u16 proto_spec = ((u16)region << 8) | RPMB_PROTOCOL_ID;
    int ret = rpmb_alloc_frame_iobuf(hba, rsp_nfrm, &iob, &frame);

    if (ret != 0) {
        return ret;
    }

    UFS_LOGI("rpmb security-in: region=%u frames=%u\n", region, rsp_nfrm);
    rpmb_prepare_security_in(&cmd, rsp_nfrm, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to receive rpmb security in response: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    memcpy(rsp_frm, frame, rsp_nfrm * sizeof(*frame));

out:
    rpmb_free_frame_iobuf(&iob);
    return ret;
}

static int rpmb_request_result_locked(struct ufs_hba* hba, u8 region, struct rpmb_data_frame* rsp_frm,
                                      u32 rsp_nfrm, u16 expected, int* result_out) {
    struct rpmb_data_frame req_frm;
    int ret;

    memset(&req_frm, 0, sizeof(req_frm));
    req_frm.req_resp = htobe16(RPMB_REQ_RESULT_RD);

    UFS_LOGI("rpmb request-result: region=%u expected=0x%x(%s) rsp_nfrm=%u\n", region,
             expected, rpmb_msg_type_to_string(expected), rsp_nfrm);

    ret = rpmb_security_out_locked(hba, region, &req_frm, 1);
    if (ret != 0) {
        return ret;
    }

    ret = rpmb_security_in_locked(hba, region, rsp_frm, rsp_nfrm);
    if (ret != 0) {
        return ret;
    }

    return rpmb_check_raw_response(rsp_frm, expected, result_out);
}

static int rpmb_read_counter_locked(struct ufs_hba* hba, u8 region, const u8 nonce[RPMB_FRM_NONCE_LEN],
                                    u32* write_counter, int* result_out) {
    struct ufs_scsi_cmd cmd;
    struct rpmb_data_frame* frame;
    ufs_iobuf_t iob;
    u16 proto_spec = ((u16)region << 8) | RPMB_PROTOCOL_ID;
    int ret = rpmb_alloc_frame_iobuf(hba, 1, &iob, &frame);

    if (ret != 0) {
        return ret;
    }

    frame->req_resp = htobe16(RPMB_REQ_READ_WR_CNT);
    memcpy(frame->nonce, nonce, RPMB_FRM_NONCE_LEN);

    rpmb_prepare_security_out(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to send rpmb counter request: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    rpmb_prepare_security_in(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to read rpmb counter response: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    ret = rpmb_check_result(frame, RPMB_RSP_READ_WR_CNT, result_out);
    if (ret != 0) {
        goto out;
    }
    if (memcmp(frame->nonce, nonce, RPMB_FRM_NONCE_LEN) != 0) {
        UFS_LOGE("rpmb nonce mismatch while reading counter\n");
        ret = -UFS_RPMB_NONCE_ERROR;
        goto out;
    }

    ret = rpmb_verify_mac(region, frame);
    if (ret != 0) {
        goto out;
    }

    *write_counter = be32toh(frame->wr_cnt);

out:
    rpmb_free_frame_iobuf(&iob);
    return ret;
}

static int rpmb_read_block_locked(struct ufs_hba* hba, u8 region, u16 addr,
                                  const u8 nonce[RPMB_FRM_NONCE_LEN], u8* data_out,
                                  int* result_out) {
    struct ufs_scsi_cmd cmd;
    struct rpmb_data_frame* frame;
    ufs_iobuf_t iob;
    u16 proto_spec = ((u16)region << 8) | RPMB_PROTOCOL_ID;
    int ret = rpmb_alloc_frame_iobuf(hba, 1, &iob, &frame);

    if (ret != 0) {
        return ret;
    }

    frame->addr = htobe16(addr);
    frame->blk_cnt = htobe16(1);
    frame->req_resp = htobe16(RPMB_REQ_AUTHEN_READ);
    memcpy(frame->nonce, nonce, RPMB_FRM_NONCE_LEN);

    rpmb_prepare_security_out(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to send rpmb read request: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    rpmb_prepare_security_in(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to receive rpmb read response: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    ret = rpmb_check_result(frame, RPMB_RSP_AUTHEN_READ, result_out);
    if (ret != 0) {
        goto out;
    }
    if (memcmp(frame->nonce, nonce, RPMB_FRM_NONCE_LEN) != 0) {
        UFS_LOGE("rpmb nonce mismatch while reading data\n");
        ret = -UFS_RPMB_NONCE_ERROR;
        goto out;
    }

    ret = rpmb_verify_mac(region, frame);
    if (ret != 0) {
        goto out;
    }

    memcpy(data_out, frame->data, RPMB_FRM_DATA_LEN);

out:
    rpmb_free_frame_iobuf(&iob);
    return ret;
}

static int rpmb_write_block_locked(struct ufs_hba* hba, u8 region, u16 addr, const u8* data,
                                   u32 write_counter, int* result_out) {
    struct ufs_scsi_cmd cmd;
    struct rpmb_data_frame* frame;
    ufs_iobuf_t iob;
    u16 proto_spec = ((u16)region << 8) | RPMB_PROTOCOL_ID;
    int ret = rpmb_alloc_frame_iobuf(hba, 1, &iob, &frame);

    if (ret != 0) {
        return ret;
    }

    memcpy(frame->data, data, RPMB_FRM_DATA_LEN);
    frame->wr_cnt = htobe32(write_counter);
    frame->addr = htobe16(addr);
    frame->blk_cnt = htobe16(1);
    frame->req_resp = htobe16(RPMB_REQ_AUTHEN_WRITE);

    ret = rpmb_get_mac(region, frame, frame->key_MAC);
    if (ret != 0) {
        goto out;
    }

    rpmb_prepare_security_out(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to send rpmb write request: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    memset(frame, 0, sizeof(*frame));
    frame->req_resp = htobe16(RPMB_REQ_RESULT_RD);
    rpmb_prepare_security_out(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to send rpmb result request: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    rpmb_prepare_security_in(&cmd, 1, proto_spec, iob.phys);
    ret = rpmb_submit_cmd(hba, &cmd, frame);
    if (ret != 0) {
        UFS_LOGE("failed to receive rpmb write response: %d\n", ret);
        ret = -UFS_RPMB_DRIVER_ERROR;
        goto out;
    }

    ret = rpmb_check_result(frame, RPMB_RSP_AUTHEN_WRITE, result_out);
    if (ret != 0) {
        goto out;
    }

    ret = rpmb_verify_mac(region, frame);
    if (ret != 0) {
        goto out;
    }

    if (be32toh(frame->wr_cnt) != (write_counter + 1)) {
        UFS_LOGE("rpmb write counter mismatch: expected=%u got=%u\n", write_counter + 1,
                 be32toh(frame->wr_cnt));
        ret = -UFS_RPMB_WRCNT_ERROR;
        goto out;
    }

out:
    rpmb_free_frame_iobuf(&iob);
    return ret;
}

int init_ufs_rpmb_sharemem(void) {
    UFS_LOGI("rpmb sharemem init\n");
    return 0;
}

int deinit_ufs_rpmb_sharemem(void) {
    UFS_LOGI("rpmb sharemem deinit\n");
    return 0;
}

int ufs_rpmb_request(u8 region, const struct rpmb_data_frame* req_frm, u32 req_nfrm,
                     struct rpmb_data_frame* rsp_frm, u32 rsp_nfrm, int* result) {
    struct ufs_hba* hba = ufs_get_host(UFS_DEFAULT_HOST_ID);
    u16 msg_type;
    int ret;

    if (result != NULL) {
        *result = 0;
    }

    if (hba == NULL || req_frm == NULL || rsp_frm == NULL || req_nfrm == 0 || rsp_nfrm == 0 ||
        req_nfrm > RPMB_MAX_FRAME_COUNT || rsp_nfrm > RPMB_MAX_FRAME_COUNT) {
        return -UFS_RPMB_DRIVER_ERROR;
    }

    msg_type = be16toh(req_frm[0].req_resp);
    UFS_LOGI("rpmb request begin: region=%u type=0x%x(%s) req_nfrm=%u rsp_nfrm=%u\n", region,
             msg_type, rpmb_msg_type_to_string(msg_type), req_nfrm, rsp_nfrm);

    mtx_lock(&hba->lock);

    switch (msg_type) {
    case RPMB_REQ_AUTHEN_KEY_PRG:
        ret = rpmb_security_out_locked(hba, region, req_frm, req_nfrm);
        if (ret == 0) {
            ret = rpmb_request_result_locked(hba, region, rsp_frm, rsp_nfrm,
                                             RPMB_RSP_AUTHEN_KEY_PRG, result);
        }
        break;
    case RPMB_REQ_READ_WR_CNT:
        ret = rpmb_security_out_locked(hba, region, req_frm, req_nfrm);
        if (ret == 0) {
            ret = rpmb_security_in_locked(hba, region, rsp_frm, rsp_nfrm);
        }
        if (ret == 0) {
            ret = rpmb_check_raw_response(rsp_frm, RPMB_RSP_READ_WR_CNT, result);
        }
        break;
    case RPMB_REQ_AUTHEN_WRITE:
        ret = rpmb_security_out_locked(hba, region, req_frm, req_nfrm);
        if (ret == 0) {
            ret = rpmb_request_result_locked(hba, region, rsp_frm, rsp_nfrm,
                                             RPMB_RSP_AUTHEN_WRITE, result);
        }
        break;
    case RPMB_REQ_AUTHEN_READ:
        ret = rpmb_security_out_locked(hba, region, req_frm, req_nfrm);
        if (ret == 0) {
            ret = rpmb_security_in_locked(hba, region, rsp_frm, rsp_nfrm);
        }
        if (ret == 0) {
            ret = rpmb_check_raw_response(rsp_frm, RPMB_RSP_AUTHEN_READ, result);
        }
        break;
    default:
        UFS_LOGE("unsupported rpmb message type: 0x%x\n", msg_type);
        ret = -UFS_RPMB_DRIVER_ERROR;
        break;
    }

    mtx_unlock(&hba->lock);

    UFS_LOGI("rpmb request done: region=%u type=0x%x(%s) ret=%d result=%d\n", region, msg_type,
             rpmb_msg_type_to_string(msg_type), ret, result != NULL ? *result : 0);

    return ret;
}

int ufs_rpmb_read(u8 region, u32 addr, u8* buf, u32 buf_len, int* result) {
    struct ufs_hba* hba = ufs_get_host(UFS_DEFAULT_HOST_ID);
    u8 nonce[RPMB_FRM_NONCE_LEN];
    u32 block_count;
    u32 index;
    int ret;

    if (hba == NULL || buf == NULL || (buf_len % RPMB_FRM_DATA_LEN) != 0) {
        return -UFS_RPMB_DRIVER_ERROR;
    }

    block_count = buf_len / RPMB_FRM_DATA_LEN;
    if (block_count == 0) {
        return 0;
    }

    UFS_LOGI("rpmb read begin: region=%u addr=%u blocks=%u\n", region, addr, block_count);

    ret = rpmb_fill_nonce(nonce, sizeof(nonce));
    if (ret != 0) {
        return ret;
    }

    mtx_lock(&hba->lock);
    for (index = 0; index < block_count; ++index) {
        ret = rpmb_read_block_locked(hba, region, (u16)(addr + index), nonce,
                                     buf + index * RPMB_FRM_DATA_LEN, result);
        if (ret != 0) {
            break;
        }
    }
    mtx_unlock(&hba->lock);

    UFS_LOGI("rpmb read done: region=%u addr=%u blocks=%u ret=%d result=%d\n", region, addr,
             block_count, ret, result != NULL ? *result : 0);

    return ret;
}

int ufs_rpmb_write(u8 region, u32 addr, u8* buf, u32 buf_len, int* result) {
    struct ufs_hba* hba = ufs_get_host(UFS_DEFAULT_HOST_ID);
    u8 nonce[RPMB_FRM_NONCE_LEN];
    u32 write_counter = 0;
    u32 block_count;
    u32 index;
    int ret;

    if (hba == NULL || buf == NULL || (buf_len % RPMB_FRM_DATA_LEN) != 0) {
        return -UFS_RPMB_DRIVER_ERROR;
    }

    block_count = buf_len / RPMB_FRM_DATA_LEN;
    if (block_count == 0) {
        return 0;
    }

    UFS_LOGI("rpmb write begin: region=%u addr=%u blocks=%u\n", region, addr, block_count);

    ret = rpmb_fill_nonce(nonce, sizeof(nonce));
    if (ret != 0) {
        return ret;
    }

    mtx_lock(&hba->lock);
    ret = rpmb_read_counter_locked(hba, region, nonce, &write_counter, result);
    if (ret == 0) {
        for (index = 0; index < block_count; ++index) {
            ret = rpmb_write_block_locked(hba, region, (u16)(addr + index),
                                          buf + index * RPMB_FRM_DATA_LEN,
                                          write_counter + index, result);
            if (ret != 0) {
                break;
            }
        }
    }
    mtx_unlock(&hba->lock);

    UFS_LOGI("rpmb write done: region=%u addr=%u blocks=%u ret=%d result=%d\n", region, addr,
             block_count, ret, result != NULL ? *result : 0);

    return ret;
}

uint64_t ufs_rpmb_get_lu_size(void) {
    struct ufs_hba* hba = ufs_get_host(UFS_DEFAULT_HOST_ID);

    if (hba == NULL) {
        return 0;
    }

    return hba->dev_info.rpmb_lu_size;
}

u32 ufs_rpmb_get_rw_size(void) {
    struct ufs_hba* hba = ufs_get_host(UFS_DEFAULT_HOST_ID);

    if (hba == NULL) {
        return 0;
    }

    return hba->dev_info.rpmb_rw_size;
}

u32 ufs_rpmb_get_setkey_flag(void) {
    struct ufs_hba* hba = ufs_get_host(UFS_DEFAULT_HOST_ID);
    u8 nonce[RPMB_FRM_NONCE_LEN];
    u32 write_counter = 0;
    int ret;

    if (hba == NULL) {
        return 1;
    }

    ret = rpmb_fill_nonce(nonce, sizeof(nonce));
    if (ret != 0) {
        memset(nonce, 0, sizeof(nonce));
    }

    mtx_lock(&hba->lock);
    ret = rpmb_read_counter_locked(hba, RPMB_REGION_0, nonce, &write_counter, NULL);
    mtx_unlock(&hba->lock);

    if (ret == 0 || ret == -UFS_RPMB_HMAC_ERROR) {
        return 0;
    }

    return 1;
}
