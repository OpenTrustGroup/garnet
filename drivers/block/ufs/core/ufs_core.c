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

#include "ufs_cfg.h"
#include "ufs_core.h"
#include "ufs_error.h"
#include "ufs_utils.h"

static void ufs_scsi_unmap_prepare(struct ufs_unmap_param_list *p, u32 lba, u32 blk_cnt)
{
    u32 exp_len;

    exp_len = sizeof(struct ufs_unmap_param_list);

    p->unmap_data_length[0] = (exp_len - 2) >> 8;
    p->unmap_data_length[1] = (exp_len - 2) & 0xFF;
    p->unmap_block_descriptor_data_length[0] = (exp_len - 8) >> 8;
    p->unmap_block_descriptor_data_length[1] = (exp_len - 8) & 0xFF;
    p->unmap_block_descriptor.unmap_logical_block_address[0] = 0;   // lba is u32 only currently
    p->unmap_block_descriptor.unmap_logical_block_address[1] = 0;   // lba is u32 only currently
    p->unmap_block_descriptor.unmap_logical_block_address[2] = 0;   // lba is u32 only currently
    p->unmap_block_descriptor.unmap_logical_block_address[3] = 0;   // lba is u32 only currently
    p->unmap_block_descriptor.unmap_logical_block_address[4] = (lba >> 24) & 0xFF;
    p->unmap_block_descriptor.unmap_logical_block_address[5] = (lba >> 16) & 0xFF;
    p->unmap_block_descriptor.unmap_logical_block_address[6] = (lba >> 8) & 0xFF;
    p->unmap_block_descriptor.unmap_logical_block_address[7] = lba & 0xFF;
    p->unmap_block_descriptor.num_logical_blocks[0] = (blk_cnt >> 24) & 0xFF;
    p->unmap_block_descriptor.num_logical_blocks[1] = (blk_cnt >> 16) & 0xFF;
    p->unmap_block_descriptor.num_logical_blocks[2] = (blk_cnt >> 8) & 0xFF;
    p->unmap_block_descriptor.num_logical_blocks[3] = blk_cnt & 0xFF;
}

int ufs_scsi_unmap(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun, u32 lba, u32 blk_cnt,
    uintptr_t paddr, u32 buf_size)
{
    u32 exp_len;

    memset(cmd, 0, sizeof(*cmd));

    exp_len = sizeof(struct ufs_unmap_param_list);

    if (buf_size < exp_len) {
        UFS_LOGE("unmap buffer size is too small buffer size=%d, exp_len=%d\n",
            buf_size, exp_len);
        return -1;
    }

    UFS_IO_DBG("exp_len=%d, lba=%d, blk_cnt=%d\n", exp_len, lba, blk_cnt);

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_TO_DEVICE;
    // UNMAP parameter list (header): 8 bytes + 1 UNMAP block descriptor: 16 bytes
    cmd->exp_len = exp_len;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 10;

    cmd->cmd_data[0] = UNMAP;               // opcode
    cmd->cmd_data[7] = 0;                   // parameter list length (MSB)
    cmd->cmd_data[8] = exp_len;             // parameter list length (LSB)
    cmd->paddr = paddr;

    return 0;
}

void ufs_scsi_cmd_sync_cache_10(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun,
    u32 lba, u32 blk_cnt)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_NONE;
    cmd->exp_len = 0;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 10;

    cmd->cmd_data[0] = SYNC_CACHE_10;       // opcode
    cmd->cmd_data[2] = (lba >> 24) & 0xff;  // LBA (MSB)
    cmd->cmd_data[3] = (lba >> 16) & 0xff;  // LBA
    cmd->cmd_data[4] = (lba >> 8) & 0xff;   // LBA
    cmd->cmd_data[5] = (lba) & 0xff;        // LBA (LSB)
    cmd->cmd_data[7] = (blk_cnt >> 8);      // transfer length (MSB)
    cmd->cmd_data[8] = blk_cnt;             // transfer length (LSB)
}

void ufs_scsi_cmd_read_capacity(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun,
    uintptr_t paddr)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_FROM_DEVICE;
    cmd->exp_len = 8;   // size of Read Capacity Parameter Data
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 10;

    cmd->cmd_data[0] = READ_CAPACITY;       // opcode
    cmd->paddr = paddr;
}

void ufs_scsi_cmd_inquiry(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun,
    uintptr_t paddr, u32 evpd, u32 page)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_FROM_DEVICE;
    cmd->exp_len = 36;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 6;

    cmd->cmd_data[0] = INQUIRY;     // opcode
    cmd->cmd_data[1] = evpd;         // evpd (0: standared INQUIRY DATA)
    cmd->cmd_data[2] = page;         // page code
    cmd->cmd_data[3] = 0x0;         // allocation length (MSB)
    cmd->cmd_data[4] = 36;          // allocation length (LSB)
    cmd->cmd_data[5] = 0x0;         // control
    cmd->paddr = paddr;
}

void ufs_scsi_cmd_read10(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun, u32 lba, u32 blk_cnt,
    uintptr_t paddr, u32 attr)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_FROM_DEVICE;
    cmd->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    cmd->attr = attr;
    cmd->cmd_len = 10;
    cmd->cmd_data[0] = READ_10;             // opcode
    // cmd->cmd_data[1] = (1 << 3);            // flags, FUA = 1
    cmd->cmd_data[1] = 0;
    cmd->cmd_data[2] = (lba >> 24) & 0xff;  // LBA (MSB)
    cmd->cmd_data[3] = (lba >> 16) & 0xff;  // LBA
    cmd->cmd_data[4] = (lba >> 8) & 0xff;   // LBA
    cmd->cmd_data[5] = (lba) & 0xff;        // LBA (LSB)
    cmd->cmd_data[6] = 0x0;                 // reserved and group number
    cmd->cmd_data[7] = (blk_cnt >> 8);      // transfer length (MSB)
    cmd->cmd_data[8] = blk_cnt;             // transfer length (LSB)
    cmd->cmd_data[9] = 0x0;                 // control
    cmd->paddr = paddr;
}

void ufs_scsi_cmd_write10(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun, u32 lba, u32 blk_cnt,
    uintptr_t paddr, u32 attr)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_TO_DEVICE;
    cmd->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    cmd->attr = attr;
    cmd->cmd_len = 10;
    cmd->cmd_data[0] = WRITE_10;            // opcode
    // cmd->cmd_data[1] = (1<<3);              // flags, FUA = 1
    cmd->cmd_data[1] = 0;
    cmd->cmd_data[2] = (lba >> 24) & 0xff;  // LBA (MSB)
    cmd->cmd_data[3] = (lba >> 16) & 0xff;  // LBA
    cmd->cmd_data[4] = (lba >> 8) & 0xff;   // LBA
    cmd->cmd_data[5] = (lba) & 0xff;        // LBA (LSB)
    cmd->cmd_data[6] = 0x0;                 // reserved and group number
    cmd->cmd_data[7] = (blk_cnt>>8);        // transfer length (MSB)
    cmd->cmd_data[8] = blk_cnt;             // transfer length (LSB)
    cmd->cmd_data[9] = 0x0;                 // control
    cmd->paddr = paddr;
}

void ufs_scsi_cmd_write_buffer(struct ufs_scsi_cmd *cmd, u32 tag, uintptr_t paddr, u32 size)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = 0;
    cmd->tag = tag;
    cmd->dir = DMA_TO_DEVICE;
    cmd->exp_len = size;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 10;
    cmd->cmd_data[0] = WRITE_BUFFER;        /* Opcode */
    cmd->cmd_data[1] = 0xE;                 /* 0xE: Download firmware */
    cmd->cmd_data[2] = 0;                   /* Buffer ID = 0 */
    cmd->cmd_data[3] = 0;                   /* Buffer Offset[23:16] = 0 */
    cmd->cmd_data[4] = 0;                   /* Buffer Offset[15:08] = 0 */
    cmd->cmd_data[5] = 0;                   /* Buffer Offset[07:00] = 0 */
    cmd->cmd_data[6] = (size >> 16) & 0xff; /* Length[23:16] */
    cmd->cmd_data[7] = (size >> 8) & 0xff;  /* Length[15:08] */
    cmd->cmd_data[8] = (size) & 0xff;       /* Length[07:00] */
    cmd->cmd_data[9] = 0x0;                 /* Control = 0 */
    cmd->paddr = paddr;
}

void ufs_scsi_cmd_test_unit_ready(struct ufs_scsi_cmd *cmd, u32 tag, u32 lun)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->lun = lun;
    cmd->tag = tag;
    cmd->dir = DMA_NONE;
    cmd->exp_len = 0;
    cmd->attr = ATTR_SIMPLE;
    cmd->cmd_len = 6;
    cmd->cmd_data[0] = TEST_UNIT_READY; //opcode
    //cmd->paddr = 0UL;
}

void ufs_scsi_cmd_start_stop_unit(struct ufs_scsi_cmd *cmd, u32 tag,
				      enum ufs_dev_pwr_mode pwr_mode)
{
	memset(cmd, 0, sizeof(*cmd));

	cmd->lun	= 0xD0;
	cmd->tag	= tag;
	cmd->dir	= DMA_NONE;
	cmd->exp_len	= 0;
	cmd->attr	= ATTR_SIMPLE;
	cmd->cmd_len	= 6;

	cmd->cmd_data[0] = SCSI_START_STOP_UNIT;	/* opcode */
	cmd->cmd_data[1] = 0x0;				/* rsvd, BIT 0: 0 */
	cmd->cmd_data[2] = 0x0;				/* rsvd */
	cmd->cmd_data[3] = 0x0;				/* rsvd */
	cmd->cmd_data[4] = pwr_mode << 4;			/* power condition: power down */
	cmd->cmd_data[5] = 0x0;				/* control */
	//cmd->paddr = 0UL;
}

int ufs_dma_set_dev_pwr(struct ufs_hba *hba, enum ufs_dev_pwr_mode pwr_mode)
{
	int ret = 0;
    struct ufshcd_lrb *lrbp;
	int tag;
	int retry = 10;
    struct ufs_scsi_cmd cmd;

	if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGE("[UFS] No free tag available tag:%x\n", tag);
        return UFS_ERR_NO_RESOURCES;;
    }

    lrbp = &hba->lrb[tag];
    lrbp->bop = NULL;
    lrbp->sync_cmd = true;
    lrbp->exp_len = 0;

	ufs_scsi_cmd_start_stop_unit(&cmd, tag, pwr_mode);

    UFS_LOGD("[UFS] set_dev_pwr begin: mode=%d tag=%d tr_db=0x%x intr=0x%x outstanding=0x%lx\n",
             pwr_mode, tag,
             ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL),
             ufshcd_readl(hba, REG_INTERRUPT_STATUS),
             hba->outstanding_reqs);

	do {
		ret = ufshcd_queuecommand(hba, &cmd);
		if (ret == 0)
			break;

		UFS_LOGW("[UFS] set_dev_pwr retry: mode=%d tag=%d ret=%d tr_db=0x%x intr=0x%x outstanding=0x%lx\n",
			 pwr_mode, tag, ret,
			 ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL),
			 ufshcd_readl(hba, REG_INTERRUPT_STATUS),
			 hba->outstanding_reqs);
		retry--;
	} while (ret && retry > 0);

	if (ret)
		UFS_LOGW("[UFS] pwr_mode:%d fail\n", pwr_mode);

    UFS_LOGI("[UFS] set_dev_pwr pwr_mode:%d\n", pwr_mode);
	return ret;
}

int ufs_dma_read(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt, uintptr_t paddr)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;

    UFS_IO_DBG("[UFS] read: L:%d,S:%d,C:%d,pa:0x%lx\n", lun, blk_start,
        blk_cnt, paddr);

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGE("[UFS] No free tag available tag:%x\n", tag);
        return UFS_ERR_NO_RESOURCES;;
    }

    lrbp = &hba->lrb[tag];
    if (bop) {
        lrbp->bop = bop;
        lrbp->sync_cmd = false;
        lrbp->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    } else {
        UFS_LOGE("[UFS] Invalid bop pointer\n");
        ufshcd_put_tag(hba, tag);
        return UFS_ERR_INVALID_ARGS;
    }

    ufs_scsi_cmd_read10(&cmd, tag, lun, blk_start, blk_cnt, paddr, ATTR_SIMPLE);
    ret = ufshcd_queuecommand(hba, &cmd);
    if (ret) {
        lrbp->cmd = NULL;
        lrbp->bop = NULL;
        ufshcd_put_tag(hba, tag);
    }
    UFS_IO_DBG("start %u, count %u, tag %u, queue command end\n", blk_start, blk_cnt, tag);

    return ret;
}

int ufs_dma_read_sg(struct ufs_hba *hba, block_op_t* bop, u32 lun,
                    u32 blk_start, u32 blk_cnt,
                    const struct ufs_dma_sg_entry* sg, u32 sg_count)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;

    if (!sg || sg_count == 0) {
        UFS_LOGE("[UFS] Invalid explicit SG read sg=%p count=%u\n", sg, sg_count);
        return UFS_ERR_INVALID_ARGS;
    }

    UFS_IO_DBG("[UFS] read_sg: L:%d,S:%d,C:%d,sg_count:%u\n", lun, blk_start,
        blk_cnt, sg_count);

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGE("[UFS] No free tag available tag:%x\n", tag);
        return UFS_ERR_NO_RESOURCES;
    }

    lrbp = &hba->lrb[tag];
    if (bop) {
        lrbp->bop = bop;
        lrbp->sync_cmd = false;
        lrbp->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    } else {
        UFS_LOGE("[UFS] Invalid bop pointer\n");
        ufshcd_put_tag(hba, tag);
        return UFS_ERR_INVALID_ARGS;
    }

    ufs_scsi_cmd_read10(&cmd, tag, lun, blk_start, blk_cnt, 0, ATTR_SIMPLE);
    cmd.sg = sg;
    cmd.sg_count = sg_count;
    ret = ufshcd_queuecommand(hba, &cmd);
    if (ret) {
        lrbp->cmd = NULL;
        lrbp->bop = NULL;
        ufshcd_put_tag(hba, tag);
    }
    UFS_IO_DBG("start %u, count %u, tag %u, queue read_sg command end\n",
               blk_start, blk_cnt, tag);

    return ret;
}

int ufs_dma_write_sg(struct ufs_hba *hba, block_op_t* bop, u32 lun,
                     u32 blk_start, u32 blk_cnt,
                     const struct ufs_dma_sg_entry* sg, u32 sg_count)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;

    if (!sg || sg_count == 0) {
        UFS_LOGE("[UFS] Invalid explicit SG write sg=%p count=%u\n", sg, sg_count);
        return UFS_ERR_INVALID_ARGS;
    }

    UFS_IO_DBG("[UFS] write_sg: L:%d,S:%d,C:%d,sg_count:%u\n", lun, blk_start,
        blk_cnt, sg_count);

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGW("[UFS] No free tag available\n");
        return UFS_ERR_NO_RESOURCES;
    }

    lrbp = &hba->lrb[tag];
    if (bop) {
        lrbp->bop = bop;
        lrbp->sync_cmd = false;
        lrbp->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    } else {
        UFS_LOGE("[UFS] Invalid bop pointer\n");
        ufshcd_put_tag(hba, tag);
        return UFS_ERR_INVALID_ARGS;
    }

    ufs_scsi_cmd_write10(&cmd, tag, lun, blk_start, blk_cnt, 0, ATTR_SIMPLE);
    cmd.sg = sg;
    cmd.sg_count = sg_count;
    ret = ufshcd_queuecommand(hba, &cmd);
    if (ret) {
        lrbp->cmd = NULL;
        lrbp->bop = NULL;
        ufshcd_put_tag(hba, tag);
    }
    UFS_IO_DBG("start %u, count %u, tag %u, queue write_sg command end\n",
               blk_start, blk_cnt, tag);

    return ret;
}

int ufs_dma_write(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt, uintptr_t paddr)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;

    UFS_IO_DBG("[UFS] write: L:%d,S:%d,C:%d,pa:0x%lx\n", lun, blk_start,
        blk_cnt, paddr);

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGW("[UFS] No free tag available\n");
        return UFS_ERR_NO_RESOURCES;;
    }

    lrbp = &hba->lrb[tag];
    if (bop) {
        lrbp->bop = bop;
        lrbp->sync_cmd = false;
        lrbp->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    } else {
        UFS_LOGE("[UFS] Invalid bop pointer\n");
        ufshcd_put_tag(hba, tag);
        return UFS_ERR_INVALID_ARGS;
    }

    ufs_scsi_cmd_write10(&cmd, tag, lun, blk_start, blk_cnt, paddr, ATTR_SIMPLE);
    ret = ufshcd_queuecommand(hba, &cmd);
    if (ret) {
        lrbp->cmd = NULL;
        lrbp->bop = NULL;
        ufshcd_put_tag(hba, tag);
    }
    UFS_IO_DBG("start %u,count %u, tag %u, queue command end\n", blk_start, blk_cnt, tag);

    return ret;
}

//#define UFS_ERASE_MEMSET

int ufs_dma_erase(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;
    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
    u8* ufs_temp_buf;
    ufs_iobuf_t* iob;

    UFS_IO_DBG("[UFS] erase: L:%d,S:%d,C:%d\n", lun, blk_start, blk_cnt);

    iob = malloc(sizeof(ufs_iobuf_t));
    UFS_ASSERT(iob != NULL);
    iob->size = UFS_TEMP_BUF_SIZE;
    iob->align = ufs->cache_line_size;
    iob->flags = IO_BUFFER_RW | IO_BUFFER_CONTIG;
    ret = ufs_alloc_iobuf(ufs, iob);
    UFS_ASSERT(ret == 0);
    ufs_temp_buf = iob->virt;

    if (!ufshcd_get_free_tag(hba, &tag)) {
        ret = -1;
        goto out;
    }

    lrbp = &hba->lrb[tag];
    if (bop) {
        lrbp->bop = bop;
        lrbp->sync_cmd = false;
        lrbp->exp_len = 0;
        lrbp->iob_unmap = (void*)iob;
    } else {
        UFS_LOGE("[UFS] Invalid bop pointer\n");
        ufshcd_put_tag(hba, tag);
        return UFS_ERR_INVALID_ARGS;
    }

    ufs_scsi_unmap_prepare((struct ufs_unmap_param_list *)ufs_temp_buf, blk_start, blk_cnt);

    ret = ufs_scsi_unmap(&cmd, tag, lun, blk_start, blk_cnt,
                         iob->phys, UFS_TEMP_BUF_SIZE);
    ufs_dma_map(iob->virt, UFS_TEMP_BUF_SIZE, DMA_TO_DEVICE);
    if (ret != 0) {
        UFS_LOGE("failed to unmap: %d\n", ret);
        ufshcd_put_tag(hba, tag);
        goto out;
    }

    ret = ufshcd_queuecommand(hba, &cmd);

    UFS_IO_DBG("[UFS] erase: T:%d,L:%d,S:%d,C:%d Done:%d\n", tag,lun, blk_start, blk_cnt, ret);

out:
    return ret;
}

int ufs_dma_secure_erase(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt)
{
    // TODO: dummmy for test
    UFS_IO_DBG("[UFS] secure_erase: L:%d,S:%d,C:%d\n", lun, blk_start, blk_cnt);
    return ufs_dma_erase(hba, bop, lun, blk_start, blk_cnt);
}

int ufs_dma_write_zeroes(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt)
{
    // TODO: dummmy for test
    UFS_IO_DBG("[UFS] write_zeroes: L:%d,S:%d,C:%d\n", lun, blk_start, blk_cnt);
    return ufs_dma_erase(hba, bop, lun, blk_start, blk_cnt);
}

int ufs_dma_flush(struct ufs_hba *hba, block_op_t* bop, u32 lun, u32 blk_start, u32 blk_cnt)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;

    UFS_IO_DBG("[UFS] info: L:%d,S:%d,C:%d\n", lun, blk_start, blk_cnt);

    if (!ufshcd_get_free_tag(hba, &tag)) {
        ret = -1;
        goto out;
    }

    lrbp = &hba->lrb[tag];
    if (bop) {
        lrbp->bop = bop;
        lrbp->sync_cmd = false;
        lrbp->exp_len = blk_cnt * UFS_BLOCK_SIZE;
    } else {
        UFS_LOGE("[UFS] Invalid bop pointer\n");
        ufshcd_put_tag(hba, tag);
        return UFS_ERR_INVALID_ARGS;
    }

    ufs_scsi_cmd_sync_cache_10(&cmd, tag, lun, blk_start, blk_cnt);

    ret = ufshcd_queuecommand(hba, &cmd);

out:
    UFS_IO_DBG("[UFS] flush: L:%d,S:%d,C:%d done: %d\n", lun, blk_start, blk_cnt, ret);

    return ret;
}

static int ufs_cfg_callback(struct ufs_hba *hba, u8 mode)
{
    int ret = 0;

    hba->mode = mode;

    if (mode == UFS_MODE_DMA) {
        hba->blk_read = ufs_dma_read;
        hba->blk_write = ufs_dma_write;
        hba->blk_erase = ufs_dma_erase;
        hba->blk_secure_erase = ufs_dma_secure_erase;
        hba->blk_write_zeroes = ufs_dma_write_zeroes;
        hba->blk_flush = ufs_dma_flush;
        hba->nopin_nopout = ufs_dma_nopin_nopout;
        hba->query_flag = ufs_dma_query_flag;
        hba->read_descriptor = ufs_dma_read_desc;
        hba->dme_get = ufs_dma_dme_get;
        hba->dme_peer_get = ufs_dma_dme_peer_get;
        hba->dme_set = ufs_dma_dme_set;
        hba->dme_peer_set = ufs_dma_dme_peer_set;
        hba->query_attr = ufs_dma_query_attr;
        hba->set_dev_pwr = ufs_dma_set_dev_pwr;
    } else {
        UFS_LOGW("[UFS] err: %d - invalid mode\n", mode);
        ret = -1;
    }

    return ret;
}

int ufs_cfg_mode(mtk_ufs_t* ufs, u8 mode)
{
    struct ufs_hba *hba = &ufs->hba;

    if (mode >= UFS_MODE_MAX) {
        UFS_LOGW("[UFS] err: unknown mode\n");
        return -1;
    }

    return ufs_cfg_callback(hba, mode);
}

int ufs_switch_part(u32 part_id)
{
    int lun;

    switch (part_id) {
    case UFS_LU_BOOT1:
        lun = UFS_LU_0;
        break;
    case UFS_LU_BOOT2:
        lun = UFS_LU_1;
        break;
    case UFS_LU_USER:
        lun = UFS_LU_2;
        break;
    default:
        UFS_LOGE("[UFS] err: invalid UFS LU %d\n", part_id);
        return UFS_ERR_INVALID_LU;
    }

    return lun;
}
