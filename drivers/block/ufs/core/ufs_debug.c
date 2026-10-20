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

#include <stdlib.h>
#include <string.h> 
#include <ddk/debug.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include "ufs_hcd.h"
#include "ufs_debug.h"

#define NUM_OF_MON	14
#define MON_DEPTH	4
#define W_PT_BITS	0x3
#define W_PT_OFFSET	22
#define W_PT_MASK	(W_PT_BITS << W_PT_OFFSET)

static struct mon_struct mon_info[NUM_OF_MON] = {
	{REG_UFS_NOPOUT_MON, "NOP OUT"},
	{REG_UFS_NOPIN_MON, "NOP IN"},
	{REG_UFS_COMMAND_MON, "COMMAND"},
	{REG_UFS_RESP_MON, "RESPONSE"},
	{REG_UFS_DATAOUT_MON, "DATA OUT"},
	{REG_UFS_DATAIN_MON, "DATA IN"},
	{REG_UFS_TMREQ_MON, "TM REQUEST"},
	{REG_UFS_TMRESP_MON, "TM RESPONSE"},
	{REG_UFS_RTT_MON, "RTT"},
	{REG_UFS_QUERYREQ_MON, "QUERY REQUEST"},
	{REG_UFS_QUERYRESP_MON, "QUERY RESPONSE"},
	{REG_UFS_REJECT_MON, "REJECT"},
	{REG_UFS_AH8E_MON, "H8 ENTER"},
	{REG_UFS_AH8X_MON, "H8 EXIT"},
};

void ufs_mtk_mon_dump(struct ufs_hba *hba)
{
	u8 i, j, sort[MON_DEPTH];
	u32 mon_w_pt[NUM_OF_MON], w_pt;
	u32 mon_data[NUM_OF_MON][MON_DEPTH];

	/* Clear buffer */
	memset(mon_data, 0, sizeof(u32) * NUM_OF_MON * MON_DEPTH);

	zxlogf(INFO, "format: LLTTtttt (LUN/TAG/timestamp in us\n");
	zxlogf(INFO, "%16s    %8s %8s %8s %8s", "", "OLDEST", "", "", "LATEST");

	/* Fetch data in a batch */
	for (i = 0; i < NUM_OF_MON; i++) {
		for (j = 0; j < MON_DEPTH; j++) {
			mon_data[i][j] = ufshcd_readl(hba, mon_info[i].offset);
			w_pt = (mon_data[i][j] >> W_PT_OFFSET) & W_PT_BITS;

			/* Get the current write pointer, and alert if not all write pointers are the same */
			if (j == 0)
				mon_w_pt[i] = w_pt;
			else if (mon_w_pt[i] != w_pt) {
				zxlogf(INFO, "w_pt[%d][0] = %x, w_pt[%d][%d] = %x\n",
					i, mon_w_pt[i], i, j, w_pt);
			}
		}

		/*
		 * Reorder according to the current write pointer
		 * the oldest record is listed first; the latest, last
		 */
		for (j = 0; j < MON_DEPTH; j++)
			sort[j] = (j + mon_w_pt[i]) % MON_DEPTH;

		zxlogf(INFO, "%16s || %08x %08x %08x %08x",
			mon_info[i].name,
			mon_data[i][sort[0]] &~ W_PT_MASK,
			mon_data[i][sort[1]] &~ W_PT_MASK,
			mon_data[i][sort[2]] &~ W_PT_MASK,
			mon_data[i][sort[3]] &~ W_PT_MASK
			);
	}
}

void ufs_mtk_ahb_dump(struct ufs_hba *hba)
{
	u32 val = ufshcd_readl(hba, REG_UFS_MMIO_DBG_AHB);

	zxlogf(INFO, "=== INPUT ===\n");
	zxlogf(INFO, "HTRANS(2) = 0x%x\n", (val >> 6) & 0x3);
	zxlogf(INFO, "HADDR(32) = 0x%x\n", ufshcd_readl(hba, REG_UFS_MMIO_DBG_AHB_HADDR));
	zxlogf(INFO, "HWRITE(1) = 0x%x\n", (val >> 8) & 0x1);
	zxlogf(INFO, "HSIZE(3) = 0x%x\n", (val >> 9) & 0x7);
	zxlogf(INFO, "HBURST(3) = 0x%x\n", (val >> 3) & 0x7);
	zxlogf(INFO, "HSECUR_B(1) = 0x%x\n", (val >> 2) & 0x1);
	zxlogf(INFO, "HREADY_IN(1) = 0x%x\n", (val >> 1) & 0x1);
	zxlogf(INFO, "HWDATA(32) = 0x%x\n", ufshcd_readl(hba, REG_UFS_MMIO_DBG_AHB_HWDATA));
	zxlogf(INFO, "=== OUTPUT ===\n");
	zxlogf(INFO, "HREADY(1) = 0x%x\n", (val & 0x1));
	zxlogf(INFO, "HRDATA(32) = 0x%x\n\n", ufshcd_readl(hba, REG_UFS_MMIO_DBG_AHB_HRDATA));

	val = ufshcd_readl(hba, REG_UFS_MMIO_DBG_NIT);
	zxlogf(INFO, "=== INPUT ===\n");
	zxlogf(INFO, "NIT_ADDR(16) = 0x%x\n", (val & 0xFFFF));
	zxlogf(INFO, "NIT_WR(1) = 0x%x\n", ((val >> 16) & 0x1));
	zxlogf(INFO, "NIT_WDATA(32) = 0x%x\n", ufshcd_readl(hba, REG_UFS_MMIO_DBG_NIT_WDATA));
	zxlogf(INFO, "=== OUTPUT ===\n");
	zxlogf(INFO, "NIT_RDATA(32) = 0x%x\n\n", ufshcd_readl(hba, REG_UFS_MMIO_DBG_NIT_RDATA));
}

void ufs_mtk_axi_dump(struct ufs_hba *hba)
{
	u64 sel_val[13] = {0};
	u32 i, val, reg;

	reg = REG_UFS_MMIO_DBG_AXIM;
	for (i = 0; i < ARRAY_SIZE(sel_val) * 2; i++) {
		val = (ufshcd_readl(hba, reg) & ~0xFF0000) | (i << 16);
		ufshcd_writel(hba, val, reg);
		val = ufshcd_readl(hba, reg) & 0xFFFF;

		if (i % 2 == 0)
			sel_val[i / 2] |= (u64)val;
		else
			sel_val[i / 2] |= (u64)val << 16;
	}

	/* mru_dbg_axim_aw */
	zxlogf(INFO, "=== mru_dbg_axim_aw ===\n");
	zxlogf(INFO, "AWID(2) = 0x%lx\n", sel_val[0] & 0x3);
	zxlogf(INFO, "AWADDR(36) = 0x%lx\n", (sel_val[0] >> 2) |
						    ((sel_val[1] & 0x3F) << 30));
	zxlogf(INFO, "AWLEN(8) = 0x%lx\n", (sel_val[1] >> 6) & 0xFF);
	zxlogf(INFO, "AWSIZE(3) = 0x%lx\n", (sel_val[1] >> 14) & 0x7);
	zxlogf(INFO, "AWBURST(2) = 0x%lx\n", (sel_val[1] >> 17) & 0x3);
	zxlogf(INFO, "AWCACHE(4) = 0x%lx\n", (sel_val[1] >> 19) & 0xF);
	zxlogf(INFO, "AWUSER(4) = 0x%lx\n\n", (sel_val[1] >> 23) & 0xF);

	/* mru_dbg_axim_w */
	zxlogf(INFO, "=== mru_dbg_axim_w ===\n");
	zxlogf(INFO, "WID(2) = 0x%lx\n", (sel_val[1] >> 27) & 0x3);
	zxlogf(INFO, "WDATA(128)[63:0] = 0x%lx\n", (sel_val[1] >> 29) |
							  (sel_val[2] << 3) |
							  ((sel_val[3] & 0x3FFFFFFF) << 35));
	zxlogf(INFO, "WDATA(128)[128:64] = 0x%lx\n", (sel_val[3] >> 29) |
							    (sel_val[4] << 3) |
							    ((sel_val[5] & 0x3FFFFFFF) << 35));
	zxlogf(INFO, "WSTRB(16) = 0x%lx\n", (sel_val[5] >> 29) |
						   ((sel_val[6] & 0x1FFF) << 3));
	zxlogf(INFO, "WLAST(1) = 0x%lx\n\n", (sel_val[6] >> 13) & 0x1);

	/* mru_dbg_axim_b */
	zxlogf(INFO, "=== mru_dbg_axim_b ===\n");
	zxlogf(INFO, "BID(2) = 0x%lx\n", (sel_val[6] >> 14) & 0x3);
	zxlogf(INFO, "BERSP(2) = 0x%lx\n\n", (sel_val[6] >> 16) & 0x3);

	/* mru_dbg_axim_ar */
	zxlogf(INFO, "=== mru_dbg_axim_ar ===\n");
	zxlogf(INFO, "ARID(2) = 0x%lx\n", (sel_val[6] >> 18) & 0x3);
	zxlogf(INFO, "ARADDR(36) = 0x%lx\n", (sel_val[6] >> 20) |
						    ((sel_val[7] & 0xFFFFFF) << 12));
	zxlogf(INFO, "ARLEN(8) = 0x%lx\n", (sel_val[7] >> 24) & 0xFF);
	zxlogf(INFO, "ARSIZE(3) = 0x%lx\n", sel_val[8] & 0x7);
	zxlogf(INFO, "ARURST(2) = 0x%lx\n", (sel_val[8] >> 3) & 0x3);
	zxlogf(INFO, "ARCACHE(4) = 0x%lx\n", (sel_val[8] >> 5) & 0xF);
	zxlogf(INFO, "ARUSER(4) = 0x%lx\n\n", (sel_val[8] >> 9) & 0xF);

	/* mru_dbg_axim_r */
	zxlogf(INFO, "=== mru_dbg_axim_r ===\n");
	zxlogf(INFO, "RID(2) = 0x%lx\n", (sel_val[8] >> 13) & 0x3);
	zxlogf(INFO, "RDATA(128)[63:0] = 0x%lx\n", (sel_val[8] >> 15) |
							  (sel_val[9] << 17) |
							  ((sel_val[10] & 0x7FFF) << 49));
	zxlogf(INFO, "RDATA(128)[128:64] = 0x%lx\n", (sel_val[10] >> 15) |
							    (sel_val[11] << 17) |
							    ((sel_val[12] & 0x7FFF) << 49));
	zxlogf(INFO, "RRESP(2) = 0x%lx\n", (sel_val[12] >> 15) & 0x3);
	zxlogf(INFO, "RLAST(1) = 0x%lx\n\n", (sel_val[12] >> 17) & 0x1);
}
