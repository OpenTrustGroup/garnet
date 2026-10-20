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
#include "ufs_platform.h"
#include "ufs_quirks.h"
#include "ufs_unipro.h"
#include "ufs_utils.h"
#include "ufs_sys.h"
#include "ufs_sip.h"
#include "ufs_debug.h"
#ifdef UFS_HW_CRYPT_EN
#include "ufshcd_crypto.h"
#endif

#define REFCLK_REQ_TIMEOUT_US       3000
#define REFCLK_DEFAULT_WAIT_US      32

#define UFS_MTK_DEFAULT_AHIT        ((0x5 << 10) | 0x0A)

static inline void ufs_mtk_auto_hibern8_enable(struct ufs_hba *hba);


static u32 ufs_query_desc_max_size[] = {
    QUERY_DESC_DEVICE_MAX_SIZE,
    QUERY_DESC_CONFIGURAION_MAX_SIZE_HPB_20,
    QUERY_DESC_UNIT_MAX_SIZE,
    QUERY_DESC_RFU_MAX_SIZE,
    QUERY_DESC_INTERCONNECT_MAX_SIZE,
    QUERY_DESC_STRING_MAX_SIZE,
    QUERY_DESC_RFU_MAX_SIZE,
    QUERY_DESC_GEOMETRY_MAX_SIZE,
    QUERY_DESC_POWER_MAX_SIZE,
    QUERY_DESC_HEALTH_MAX_SIZE,
    QUERY_DESC_RFU_MAX_SIZE,
};
typedef struct {
    const char *name;
    int value;
} EnumRegMap;

EnumRegMap enum_reg_map[] = {
    {"REG_CONTROLLER_CAPABILITIES",              0x00},
    {"REG_UFS_VERSION",                          0x08},
    {"REG_CONTROLLER_DEV_ID",                    0x10},
    {"REG_CONTROLLER_PROD_ID",                   0x14},
    {"REG_AHIT",                                 0x18},
    {"REG_INTERRUPT_STATUS",                     0x20},
    {"REG_INTERRUPT_ENABLE",                     0x24},
    {"REG_CONTROLLER_STATUS",                    0x30},
    {"REG_CONTROLLER_ENABLE",                    0x34},
    {"REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER",     0x38},
    {"REG_UIC_ERROR_CODE_DATA_LINK_LAYER",       0x3C},
    {"REG_UIC_ERROR_CODE_NETWORK_LAYER",         0x40},
    {"REG_UIC_ERROR_CODE_TRANSPORT_LAYER",       0x44},
    {"REG_UIC_ERROR_CODE_DME",                   0x48},
    {"REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL",     0x4C},
    {"REG_UTP_TRANSFER_REQ_LIST_BASE_L",         0x50},
    {"REG_UTP_TRANSFER_REQ_LIST_BASE_H",         0x54},
    {"REG_UTP_TRANSFER_REQ_DOOR_BELL",           0x58},
    {"REG_UTP_TRANSFER_REQ_LIST_CLEAR",          0x5C},
    {"REG_UTP_TRANSFER_REQ_LIST_RUN_STOP",       0x60},
    {"REG_UTP_TRANSFER_REQ_LIST_COMP_NOTIFY",    0x64},
    {"REG_UTP_TASK_REQ_LIST_BASE_L",             0x70},
    {"REG_UTP_TASK_REQ_LIST_BASE_H",             0x74},
    {"REG_UTP_TASK_REQ_DOOR_BELL",               0x78},
    {"REG_UTP_TASK_REQ_LIST_CLEAR",              0x7C},
    {"REG_UTP_TASK_REQ_LIST_RUN_STOP",           0x80},
    {"REG_UIC_COMMAND",                          0x90},
    {"REG_UIC_COMMAND_ARG_1",                    0x94},
    {"REG_UIC_COMMAND_ARG_2",                    0x98},
    {"REG_UIC_COMMAND_ARG_3",                    0x9C},
    {"REG_UMABA",                                0xB0},
    {"REG_UMABAU",                               0xB4},
    {"REG_UMAOMAX",                              0xB8},
    {"REG_UMACONF",                              0xBC},
    {"REG_XCNF",                                 0xC8},
    {"REG_CDACFG",                               0xD0},
    {"REG_CDATX1",                               0xD4},
    {"REG_CDATX2",                               0xD8},
    {"REG_CDARX1",                               0xDC},
    {"REG_CDARX2",                               0xE0},
    {"REG_CDASTA",                               0xE4},
    {"REG_CRYPTO_CAPABILITY",                    0x100},
    {"REG_CRYPTO_CAPABILITY_X",                  0x104},
    {"REG_EXTREG1",                              0x2100},
    {"REG_MPHYCTRL",                             0x2200},
    {"REG_MTK_AUTO_DEEP_STALL",                  0x2210},
    {"REG_UFS_MTK_HW_VER",                       0x2240},
    {"REG_UFS_MTK_OCS_ERR_STATUS",               0x2244},
};


/* UFSHCD UIC layer error flags */
enum {
	UFSHCD_UIC_DL_PA_INIT_ERROR = (1 << 0), /* Data link layer error */
	UFSHCD_UIC_DL_NAC_RECEIVED_ERROR = (1 << 1), /* Data link layer error */
	UFSHCD_UIC_DL_TCx_REPLAY_ERROR = (1 << 2), /* Data link layer error */
	UFSHCD_UIC_NL_ERROR = (1 << 3), /* Network layer error */
	UFSHCD_UIC_TL_ERROR = (1 << 4), /* Transport Layer error */
	UFSHCD_UIC_DME_ERROR = (1 << 5), /* DME error */
	UFSHCD_UIC_PA_GENERIC_ERROR = (1 << 6), /* Generic PA error */
};

static struct ufs_hba* g_ufs_hba = NULL;
static void ufshcd_init_desc_sizes(struct ufs_hba *hba);
static void ufs_reset_device(struct ufs_hba *hba);
static void ufs_util_dump_config_table_hex(u8 *table, u32 bytes,
    u32 ud0_base_offset, u32 ud_config_len);
static void ufs_util_dump_table_hex(u8 *table, u32 bytes);
static int ufshcd_reset_and_restore(struct ufs_hba *hba);
static void ufshcd_dump_uic_error(struct ufs_hba *hba);
static void ufshcd_complete_requests(struct ufs_hba *hba);
static int ufshcd_err_handler(void* arg);

#define UFS_EH_TRIGGER      ZX_USER_SIGNAL_0
#define UFS_EH_SHUTDOWN     ZX_USER_SIGNAL_1
#define UFS_EH_SHUTDOWN_DONE ZX_USER_SIGNAL_2

static int ufshcd_eh_thread(void* arg)
{
    struct ufs_hba* hba = (struct ufs_hba*)arg;

    while (true) {
        uint32_t pending = 0;
        zx_status_t status = zx_object_wait_one(hba->eh_event,
                                                UFS_EH_TRIGGER | UFS_EH_SHUTDOWN,
                                                ZX_TIME_INFINITE, &pending);
        if (status != ZX_OK) {
            UFS_LOGE("eh thread wait failed, ret=%d\n", status);
            break;
        }

        if (pending & UFS_EH_SHUTDOWN) {
            zx_object_signal(hba->eh_event, pending, UFS_EH_SHUTDOWN_DONE);
            break;
        }

        if (pending & UFS_EH_TRIGGER) {
            zx_object_signal(hba->eh_event, UFS_EH_TRIGGER, 0);
            ufshcd_err_handler(hba);
        }
    }

    UFS_LOGW("eh thread terminated\n");
    return 0;
}

static int ufshcd_eh_init(struct ufs_hba *hba)
{
    zx_status_t status = zx_event_create(0u, &hba->eh_event);
    if (status != ZX_OK) {
        UFS_LOGE("failed to create eh event\n");
        return status;
    }

    int ret = thrd_create_with_name(&hba->eh_thread, ufshcd_eh_thread, hba, "ufs-eh");
    if (ret != thrd_success) {
        UFS_LOGE("failed to create eh thread\n");
        zx_handle_close(hba->eh_event);
        hba->eh_event = ZX_HANDLE_INVALID;
        return ZX_ERR_INTERNAL;
    }

    hba->eh_thread_running = true;
    return ZX_OK;
}

static int ufshcd_read_desc_param(struct ufs_hba *hba,
                                  enum desc_idn desc_id,
                                  int desc_index,
                                  u8 selector,
                                  u32 param_offset,
                                  u8 *param_read_buf,
                                  u32 param_size);

static int ufshcd_dump_regs(struct ufs_hba *hba, size_t offset, size_t len,
			    const char *prefix)
{
	u32 *regs;
	size_t pos;

	if (offset % 4 != 0 || len % 4 != 0) /* keep readl happy */
		return -1;

	regs = malloc(len);

	if (!regs)
		return -1;

	for (pos = 0; pos < len; pos += 4) {
		if (offset == 0 &&
		    pos >= REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER &&
		    pos <= REG_UIC_ERROR_CODE_DME)
			continue;
		regs[pos / 4] = ufshcd_readl(hba, offset + pos);
	}
	UFS_LOGE("%s", prefix);
	ufs_util_dump_table_hex((u8 *)regs, len);
	free(regs);

	return 0;
}

static void ufs_dbg_register_dump(struct ufs_hba *hba)
{
	if (hba->force_reset)
		return;

	/*
	 * Skip debug dump since critical information has already
	 * been printed previously before the eh work was scheduled.
	 */
	if (ufshcd_eh_in_progress(hba))
		return;

	ufs_mtk_ahb_dump(hba);
	ufs_mtk_axi_dump(hba);
	ufs_mtk_mon_dump(hba);

	//mt_irq_dump_status(hba->irq);

	/* Dump ufshci register 0x0 ~ 0x9C */
	ufshcd_dump_regs(hba, 0, REG_SPACE_SIZE, "UFSHCI (0x0):");
	/* Dump ufshci register 0x100 ~ 0x160 */
	ufshcd_dump_regs(hba, REG_CRYPTO_CAPABILITY,
			 REG_UFS_MMIO_OPT_CTRL_0 - REG_CRYPTO_CAPABILITY + 4,
			 "UFSHCI (0x100): ");
	/* Dump ufshci register 0x170 */
	ufshcd_dump_regs(hba, REG_UFS_MMIO_STS_0, 4, "UFSHCI (0x170): ");
	/* Dump ufshci register 0x300 */
	ufshcd_dump_regs(hba, REG_UFS_MEM_CFG, 4, "UFSHCI (0x300): ");

	/* Dump ufshci register 0x2100 */
	ufshcd_dump_regs(hba, REG_UFS_EXTREG, 0x4, "Ext Reg (0x2100): ");

	/* Dump ufshci register 0x2200 ~ 0x2248 */
	ufshcd_dump_regs(hba, REG_UFS_MPHYCTRL,
			 REG_UFS_MTK_STATUS - REG_UFS_MPHYCTRL + 4,
			 "MPHY Ctrl (0x2200): ");
}

static void ufshcd_print_host_state(struct ufs_hba *hba)
{
	UFS_LOGE("outstanding reqs=0x%lx\n", hba->outstanding_reqs);
	UFS_LOGE("saved_err=0x%x, saved_uic_err=0x%x\n",
		 hba->saved_err, hba->saved_uic_err);
	UFS_LOGE("error handling flags=0x%x\n", hba->eh_flags);
}

/**
 * ufshcd_print_pwr_info - print power params as saved in hba
 * power info
 * @hba: per-adapter instance
 */
static void ufshcd_print_pwr_info(struct ufs_hba *hba)
{
	static const char * const names[] = {
		"INVALID MODE",
		"FAST MODE",
		"SLOW_MODE",
		"INVALID MODE",
		"FASTAUTO_MODE",
		"SLOWAUTO_MODE",
		"INVALID MODE",
	};

	/*
	 * Using dev_dbg to avoid messages during runtime PM to avoid
	 * never-ending cycles of messages written back to storage by user space
	 * causing runtime resume, causing more messages and so on.
	 */
	UFS_LOGI("[RX, TX]: gear=[%d, %d], lane[%d, %d], pwr[%s, %s], rate = %d\n",
		 hba->pwr_info.gear_rx, hba->pwr_info.gear_tx,
		 hba->pwr_info.lane_rx, hba->pwr_info.lane_tx,
		 names[hba->pwr_info.pwr_rx],
		 names[hba->pwr_info.pwr_tx],
		 hba->pwr_info.hs_rate);
}

static void ufshcd_print_evt(struct ufs_hba *hba, u32 id,
			     const char *err_name)
{
	int i;
	bool found = false;
	const struct ufs_event_hist *e;

	if (id >= UFS_EVT_CNT)
		return;

	e = &hba->ufs_stats.event[id];

	for (i = 0; i < UFS_EVENT_HIST_LENGTH; i++) {
		int p = (i + e->pos) % UFS_EVENT_HIST_LENGTH;

		if (e->tstamp[p] == 0)
			continue;
		UFS_LOGE("%s[%d] = 0x%x at %ld us\n", err_name, p,
			 e->val[p], e->tstamp[p] / 1000);
		found = true;
	}

	if (!found)
		UFS_LOGE("No record of %s\n", err_name);
	else
		UFS_LOGE("%s: total cnt=%llu\n", err_name, e->cnt);
}

static void ufshcd_print_evt_hist(struct ufs_hba *hba)
{
	ufshcd_dump_regs(hba, 0, REG_SPACE_SIZE, "host_regs: ");

	ufshcd_print_evt(hba, UFS_EVT_PA_ERR, "pa_err");
	ufshcd_print_evt(hba, UFS_EVT_DL_ERR, "dl_err");
	ufshcd_print_evt(hba, UFS_EVT_NL_ERR, "nl_err");
	ufshcd_print_evt(hba, UFS_EVT_TL_ERR, "tl_err");
	ufshcd_print_evt(hba, UFS_EVT_DME_ERR, "dme_err");
	ufshcd_print_evt(hba, UFS_EVT_AUTO_HIBERN8_ERR,
			 "auto_hibern8_err");
	ufshcd_print_evt(hba, UFS_EVT_FATAL_ERR, "fatal_err");
	ufshcd_print_evt(hba, UFS_EVT_LINK_STARTUP_FAIL,
			 "link_startup_fail");
	ufshcd_print_evt(hba, UFS_EVT_RESUME_ERR, "resume_fail");
	ufshcd_print_evt(hba, UFS_EVT_SUSPEND_ERR,
			 "suspend_fail");
	ufshcd_print_evt(hba, UFS_EVT_WL_RES_ERR, "wlun resume_fail");
	ufshcd_print_evt(hba, UFS_EVT_WL_SUSP_ERR,
			 "wlun suspend_fail");
	ufshcd_print_evt(hba, UFS_EVT_DEV_RESET, "dev_reset");
	ufshcd_print_evt(hba, UFS_EVT_HOST_RESET, "host_reset");
	ufshcd_print_evt(hba, UFS_EVT_ABORT, "task_abort");

	ufs_dbg_register_dump(hba);
}

static void ufshcd_schedule_eh_thread(struct ufs_hba *hba)
{
	UFS_LOGE("schedule eh\n");
	if (hba->eh_event != ZX_HANDLE_INVALID) {
		zx_object_signal(hba->eh_event, 0, UFS_EH_TRIGGER);
		return;
	}
	UFS_LOGE("eh worker not ready, skip synchronous handler\n");
}

void ufshcd_eh_exit(struct ufs_hba *hba)
{
    if (!hba->eh_thread_running) {
        return;
    }

    zx_object_signal(hba->eh_event, 0, UFS_EH_SHUTDOWN);
    zx_object_wait_one(hba->eh_event, UFS_EH_SHUTDOWN_DONE, ZX_TIME_INFINITE, NULL);
    thrd_join(hba->eh_thread, NULL);
    hba->eh_thread_running = false;

    if (hba->eh_event != ZX_HANDLE_INVALID) {
        zx_handle_close(hba->eh_event);
        hba->eh_event = ZX_HANDLE_INVALID;
    }
}

static void ufshcd_event_notify(struct ufs_hba *hba,
					    enum ufs_event_type evt,
					    void *data)
{
	unsigned int val = *(u32 *)data;
	unsigned long reg;
	u8 bit;

	/* Print details of UIC Errors */
	if (evt <= UFS_EVT_DME_ERR) {
		UFS_LOGE("Host UIC Error Code (%s): %08x\n",
			 ufs_uic_err_str[evt], val);
		reg = val;
	}

	if (evt == UFS_EVT_PA_ERR) {
		for_each_set_bit(bit, &reg, min(sizeof(reg) * 8, ARRAY_SIZE(ufs_uic_pa_err_str)))
			UFS_LOGE("%s\n", ufs_uic_pa_err_str[bit]);

		if ((reg & UIC_PHY_ADAPTER_LAYER_ERROR_CODE_MASK) == 0x3) {
			UFS_LOGE("force reset only 0x80000003!\n");
			mtx_lock(&hba->free_tag_lock);
			hba->force_reset = true;
			mtx_unlock(&hba->free_tag_lock);

			/* Disable UIC error intr to stop consecutive irq */
			ufshcd_writel(hba, UFSHCD_ERROR_MASK, REG_INTERRUPT_ENABLE);

			ufshcd_schedule_eh_thread(hba);
		} else {
			ufs_dbg_register_dump(hba);
		}
	}

	if (evt == UFS_EVT_FATAL_ERR) {
		/* intr_status */
		UFS_LOGE(" UFS_EVT_FATAL_ERR\n");
		reg = val;
		if (reg & (CONTROLLER_FATAL_ERROR | SYSTEM_BUS_FATAL_ERROR)) {
			UFS_LOGE(" UFS_EVT_FATAL_ERR 1\n");
			//ufshcd_writel(hba, UFSHCD_ERROR_MASK, REG_INTERRUPT_ENABLE);
			/* for fatal error, eh will be queued automatically */
		}
	}

	if (evt == UFS_EVT_DL_ERR) {
		for_each_set_bit(bit, &reg, min(sizeof(reg) * 8, ARRAY_SIZE(ufs_uic_pa_err_str)))
			UFS_LOGE("%s\n", ufs_uic_dl_err_str[bit]);

		if (!hba->force_reset)
			ufs_dbg_register_dump(hba);
	}
}

void ufshcd_update_evt_hist(struct ufs_hba *hba, u32 id, u32 val)
{
	struct ufs_event_hist *e;

	if (id >= UFS_EVT_CNT)
		return;

	e = &hba->ufs_stats.event[id];
	e->val[e->pos] = val;
	e->tstamp[e->pos] = zx_ticks_get();
	e->cnt += 1;
	e->pos = (e->pos + 1) % UFS_EVENT_HIST_LENGTH;

	ufshcd_event_notify(hba, id, &val);
}

static void ufshcd_update_uic_error(struct ufs_hba *hba)
{
	u32 reg;

	/* PHY layer error */
	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
	if ((reg & UIC_PHY_ADAPTER_LAYER_ERROR) &&
	    (reg & UIC_PHY_ADAPTER_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_PA_ERR, reg);
		/*
		 * To know whether this error is fatal or not, DB timeout
		 * must be checked but this error is handled separately.
		 */
		if (reg & UIC_PHY_ADAPTER_LAYER_LANE_ERR_MASK)
			UFS_LOGE("UIC Lane error reported\n");

		/* Got a LINERESET indication. */
		if (reg & UIC_PHY_ADAPTER_LAYER_GENERIC_ERROR) {
			struct uic_command *cmd = NULL;

			hba->uic_error |= UFSHCD_UIC_PA_GENERIC_ERROR;
			if (hba->active_uic_cmd)
				cmd = hba->active_uic_cmd;
			/*
			 * Ignore the LINERESET during power mode change
			 * operation via DME_SET command.
			 */
			if (cmd && (cmd->command == UIC_CMD_DME_SET))
				hba->uic_error &= ~UFSHCD_UIC_PA_GENERIC_ERROR;
		}
	}

	/* PA_INIT_ERROR is fatal and needs UIC reset */
	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_DATA_LINK_LAYER);
	if ((reg & UIC_DATA_LINK_LAYER_ERROR) &&
	    (reg & UIC_DATA_LINK_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_DL_ERR, reg);

		if (reg & UIC_DATA_LINK_LAYER_ERROR_PA_INIT)
			hba->uic_error |= UFSHCD_UIC_DL_PA_INIT_ERROR;
		if (reg & UIC_DATA_LINK_LAYER_ERROR_NAC_RECEIVED)
			hba->uic_error |= UFSHCD_UIC_DL_NAC_RECEIVED_ERROR;
		if (reg & UIC_DATA_LINK_LAYER_ERROR_TCx_REPLAY_TIMEOUT)
			hba->uic_error |= UFSHCD_UIC_DL_TCx_REPLAY_ERROR;
	}

	/* UIC NL/TL/DME errors needs software retry */
	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_NETWORK_LAYER);
	if ((reg & UIC_NETWORK_LAYER_ERROR) &&
	    (reg & UIC_NETWORK_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_NL_ERR, reg);
		hba->uic_error |= UFSHCD_UIC_NL_ERROR;
	}

	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_TRANSPORT_LAYER);
	if ((reg & UIC_TRANSPORT_LAYER_ERROR) &&
	    (reg & UIC_TRANSPORT_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_TL_ERR, reg);
		hba->uic_error |= UFSHCD_UIC_TL_ERROR;
	}

	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_DME);
	if ((reg & UIC_DME_ERROR) &&
	    (reg & UIC_DME_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_DME_ERR, reg);
		hba->uic_error |= UFSHCD_UIC_DME_ERROR;
	}

	UFS_LOGE("UIC error flags = 0x%08x\n", hba->uic_error);
}

static inline bool ufshcd_is_saved_err_fatal(struct ufs_hba *hba)
{
	return (hba->saved_uic_err & UFSHCD_UIC_DL_PA_INIT_ERROR) ||
	       (hba->saved_err & (INT_FATAL_ERRORS | UFSHCD_UIC_HIBERN8_MASK));
}

static int ufshcd_err_handler(void* arg)
{
	struct ufs_hba* hba = (struct ufs_hba*)arg;
	int err;

	UFS_LOGE(" enter\n");

	mtx_lock(&hba->free_tag_lock);
	if (ufshcd_eh_in_progress(hba)) {
		mtx_unlock(&hba->free_tag_lock);
		return 0;
	}
	ufshcd_set_eh_in_progress(hba);
	if (hba->saved_err || hba->saved_uic_err)
		UFS_LOGE("exit: saved_err 0x%x saved_uic_err 0x%x", hba->saved_err, hba->saved_uic_err);
	mtx_unlock(&hba->free_tag_lock);

	ufshcd_complete_requests(hba);   //TODO
	if ((hba->saved_err & (INT_FATAL_ERRORS | UFSHCD_UIC_HIBERN8_MASK)) ||
	    (hba->saved_uic_err &&
	     (hba->saved_uic_err != UFSHCD_UIC_PA_GENERIC_ERROR))) {
		ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
		ufshcd_print_evt_hist(hba);
	}
	/*
	 * if host reset is required then skip clearing the pending
	 * transfers forcefully because they will get cleared during
	 * host reset and restore
	 */
	if (hba->force_reset || ufshcd_is_saved_err_fatal(hba) ||
	    ((hba->saved_err & UIC_ERROR) &&
	     (hba->saved_uic_err & (UFSHCD_UIC_DL_NAC_RECEIVED_ERROR |
				    UFSHCD_UIC_DL_TCx_REPLAY_ERROR)))) {
		hba->force_reset = false;
		err = ufshcd_reset_and_restore(hba);
		if (err)
			UFS_LOGE("reset and restore failed with err %d\n", err);
		mtx_lock(&hba->free_tag_lock);
		ufshcd_clear_eh_in_progress(hba);
		mtx_unlock(&hba->free_tag_lock);
		ufs_wake_worker_thread(hba);

		return 0;
	}
	mtx_lock(&hba->free_tag_lock);
	ufshcd_clear_eh_in_progress(hba);
	mtx_unlock(&hba->free_tag_lock);
	ufs_wake_worker_thread(hba);

	return 0;
}

static int ufshcd_check_errors(struct ufs_hba *hba, u32 intr_status)
{
	bool queue_eh_work = false;
	int retval = 0;

	UFS_LOGE("[UFS] intr_status: 0x%x\n", intr_status);
	mtx_lock(&hba->free_tag_lock);

	if (intr_status & UIC_ERROR)
		ufshcd_dump_uic_error(hba);

	//TODO:hibernate8 enter or exit error status update
	hba->errors |= UFSHCD_ERROR_MASK & intr_status;

	if (hba->errors & INT_FATAL_ERRORS) {
		ufshcd_update_evt_hist(hba, UFS_EVT_FATAL_ERR,
				       hba->errors);
		queue_eh_work = true;
	}

	if (hba->errors & UIC_ERROR) {
		hba->uic_error = 0;
		ufshcd_update_uic_error(hba);
		if (hba->uic_error)
			queue_eh_work = true;
	}

	if (hba->errors & UFSHCD_UIC_HIBERN8_MASK) {
		UFS_LOGT("Auto Hibern8 %s failed - status: 0x%08x\n", (hba->errors & UIC_HIBERNATE_ENTER) ?
			 "Enter" : "Exit",
			 hba->errors);
		ufshcd_update_evt_hist(hba, UFS_EVT_AUTO_HIBERN8_ERR,
				       hba->errors);
		queue_eh_work = true;
	}

	if (queue_eh_work) {
		/*
		 * update the transfer error masks to sticky bits, let's do this
		 * irrespective of current ufshcd_state.
		 */
		hba->saved_err |= hba->errors;
		hba->saved_uic_err |= hba->uic_error;

		/* dump controller state before resetting */
		if (((hba->saved_err &
		      (INT_FATAL_ERRORS | UFSHCD_UIC_HIBERN8_MASK)) ||
		     (hba->saved_uic_err &&
		      (hba->saved_uic_err != UFSHCD_UIC_PA_GENERIC_ERROR)))) {
			UFS_LOGT("saved_err 0x%x saved_uic_err 0x%x\n", hba->saved_err,
				 hba->saved_uic_err);
			ufshcd_dump_regs(hba, 0, REG_SPACE_SIZE,
					 "host_regs: ");
			ufshcd_print_pwr_info(hba);
		}
		ufshcd_schedule_eh_thread(hba);
	}

	hba->errors = 0;
	hba->uic_error = 0;
	ufshcd_writel(hba, UFSHCD_ERROR_MASK, REG_INTERRUPT_STATUS);
	mtx_unlock(&hba->free_tag_lock);
	return retval;
}


/**
 * ufshcd_get_tr_ocs - Get the UTRD Overall Command Status
 * @lrb: pointer to local command reference block
 *
 * This function is used to get the OCS field from UTRD
 * Returns the OCS field in the UTRD
 */
static inline int ufshcd_get_tr_ocs(struct ufshcd_lrb *lrbp)
{
    return (lrbp->utr_descriptor_ptr->header.dword_2) & MASK_OCS;
}

static inline int ufshcd_get_req_rsp(struct utp_upiu_rsp *ucd_rsp_ptr)
{
    return __builtin_bswap32(ucd_rsp_ptr->header.dword_0) >> 24;
}

/**
 * ufshcd_get_rsp_upiu_result - Get the result from response UPIU
 * @ucd_rsp_ptr: pointer to response UPIU
 *
 * This function gets the response status and scsi_status from response UPIU
 * Returns the response result code.
 */
static inline int
ufshcd_get_rsp_upiu_result(struct utp_upiu_rsp *ucd_rsp_ptr)
{
    return __builtin_bswap32(ucd_rsp_ptr->header.dword_1) & MASK_RSP_UPIU_RESULT;
}

static int ufshcd_copy_query_response(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    struct ufs_query_res *query_res = &hba->dev_cmd.query.response;

    memcpy(&query_res->upiu_res, &lrbp->ucd_rsp_ptr->qr, QUERY_OSF_SIZE);

    /* Get the descriptor */
    if (lrbp->ucd_rsp_ptr->qr.opcode == UPIU_QUERY_OPCODE_READ_DESC) {
        u8 *descp = (u8 *)lrbp->ucd_rsp_ptr +
                    GENERAL_UPIU_REQUEST_SIZE;
        u16 resp_len;
        u16 buf_len;

        /* data segment length */
        resp_len = __builtin_bswap32(lrbp->ucd_rsp_ptr->header.dword_2) &
                   MASK_QUERY_DATA_SEG_LEN;
        buf_len = __builtin_bswap16(
                      hba->dev_cmd.query.request.upiu_req.length);
        if (buf_len >= resp_len) {
            /*
             * ufshcd_wait_command() only invalidates length of "strcut utp_upiu_rsp".
             * We need to invalidate more data range for Read Descriptor operation.
             */
            ufs_dma_unmap(descp, resp_len, DMA_FROM_DEVICE);
            memcpy(hba->dev_cmd.query.descriptor, descp, resp_len);
        } else {
            UFS_LOGW("Response size is bigger than buffer");
            return -1;
        }
    }

    return 0;
}

static int ufshcd_dev_cmd_completion(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    int err = 0;
    int resp;

    resp = ufshcd_get_req_rsp(lrbp->ucd_rsp_ptr);

    switch (resp) {
    case UPIU_TRANSACTION_REJECT_UPIU:
        /* TODO: handle Reject UPIU Response */
        err = -1;
        UFS_LOGW("Reject UPIU not fully implemented\n");
        break;

    case UPIU_TRANSACTION_QUERY_RSP:
        hba->dev_cmd.query.response.response = ufshcd_get_rsp_upiu_result(lrbp->ucd_rsp_ptr)
            >> UPIU_RSP_CODE_OFFSET;
        err = hba->dev_cmd.query.response.response;
        if (!err)
            err = ufshcd_copy_query_response(hba, lrbp);
        break;

    case UPIU_TRANSACTION_NOP_IN:
        if (hba->dev_cmd.type != DEV_CMD_TYPE_NOP) {
            err = -1;
            UFS_LOGW("unexpected response %x\n", resp);
        }
        break;

    default:
        err = -1;
        UFS_LOGW("Invalid device management cmd response: %x\n", resp);
        break;
    }

    return err;
}

int ufshcd_query_flag_retry(struct ufs_hba *hba,
	enum query_opcode opcode, enum flag_idn idn, u8 index, bool *flag_res)
{
	int ret;
	int retries;

	for (retries = 0; retries < QUERY_REQ_RETRIES; retries++) {
        ret = hba->query_flag(hba, opcode, idn, index, flag_res);
		if (ret)
			UFS_LOGW("failed with error %d, retries %d\n", ret, retries);
		else
			break;
	}

	if (ret) {
		UFS_LOGE("query flag, opcode %d, idn %d, failed with error %d after %d retries\n",
			opcode, idn, ret, retries);
    }

	return ret;
}

int ufshcd_query_attr_retry(struct ufs_hba *hba,
	enum query_opcode opcode, enum attr_idn idn, u8 index, u8 selector,
	u32 *attr_val)
{
	int ret = 0;
	u32 retries;

	for (retries = QUERY_REQ_RETRIES; retries > 0; retries--) {
        ret = hba->query_attr(hba, opcode, idn, index,
						selector, attr_val);
		if (ret)
			UFS_LOGW("failed with error %d, retries %d\n", ret, retries);
		else
			break;
	}

	if (ret) {
		UFS_LOGE("query attribute, idn %d, failed with error %d after %d retries\n",
			idn, ret, QUERY_REQ_RETRIES);
    }

	return ret;
}

static inline bool ufshcd_is_wb_allowed(struct ufs_hba *hba)
{
	return hba->wb_support;
}

static bool ufshcd_is_wb_buf_flush_allowed(struct ufs_hba *hba)
{
	return ufshcd_is_wb_allowed(hba) &&
		!(hba->hci_quirks & UFS_DEVICE_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL);
}

int ufshcd_wb_toggle(struct ufs_hba *hba, bool enable)
{
	int ret;
    u8 index;
    bool flag_res = true;

	if (!ufshcd_is_wb_allowed(hba) ||
	    hba->dev_info.wb_enabled == enable)
		return 0;

    enum query_opcode opcode = enable ? UPIU_QUERY_OPCODE_SET_FLAG :
				   UPIU_QUERY_OPCODE_CLEAR_FLAG;
    if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		index = hba->dev_info.wb_dedicated_lu;
    else
        index = 0;

    ret = ufshcd_query_flag_retry(hba, opcode, QUERY_FLAG_IDN_WB_EN,
                      index, &flag_res);
	if (ret) {
		UFS_LOGE("Write Booster %s failed %d\n", enable ? "enabling" : "disabling", ret);
		return ret;
	}

	hba->dev_info.wb_enabled = enable;
	UFS_LOGI("Write Booster %s\n", enable ? "enabled" : "disabled");

	return ret;
}

static void ufshcd_wb_toggle_buf_flush_during_h8(struct ufs_hba *hba,
						 bool enable)
{
	int ret;
    u8 index;
    bool flag_res = true;

    enum query_opcode opcode = enable ? UPIU_QUERY_OPCODE_SET_FLAG :
				   UPIU_QUERY_OPCODE_CLEAR_FLAG;
    if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		index = hba->dev_info.wb_dedicated_lu;
    else
        index = 0;

    ret = ufshcd_query_flag_retry(hba, opcode,
                      QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8,
                      index, &flag_res);
	if (ret) {
		UFS_LOGE("WB-Buf Flush during H8 %s failed %d\n",enable ? "enabling" : "disabling", ret);
		return;
	}
	UFS_LOGI("WB-Buf Flush during H8 %s\n", enable ? "enabled" : "disabled");
}

int ufshcd_wb_toggle_buf_flush(struct ufs_hba *hba, bool enable)
{
	int ret;
    u8 index;
    bool flag_res = true;

	if (!ufshcd_is_wb_allowed(hba) ||
	    hba->dev_info.wb_buf_flush_enabled == enable)
		return 0;

    enum query_opcode opcode = enable ? UPIU_QUERY_OPCODE_SET_FLAG :
				   UPIU_QUERY_OPCODE_CLEAR_FLAG;
    if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		index = hba->dev_info.wb_dedicated_lu;
    else
        index = 0;

    ret = ufshcd_query_flag_retry(hba, opcode,
                      QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN,
                      index, &flag_res);
	if (ret) {
		UFS_LOGE("WB-Buf Flush %s failed %d\n", enable ? "enabling" : "disabling", ret);
		return ret;
	}

	hba->dev_info.wb_buf_flush_enabled = enable;
	UFS_LOGI("WB-Buf Flush %s\n",  enable ? "enabled" : "disabled");

	return ret;
}

static void ufshcd_configure_wb(struct ufs_hba *hba)
{
	if (!ufshcd_is_wb_allowed(hba))
		return;
	ufshcd_wb_toggle(hba, true);
	ufshcd_wb_toggle_buf_flush_during_h8(hba, true);
	if (ufshcd_is_wb_buf_flush_allowed(hba))
		ufshcd_wb_toggle_buf_flush(hba, true);
}

static bool ufshcd_is_wb_buf_lifetime_available(struct ufs_hba *hba)
{
	u32 lifetime;
	int ret;
	u8 index;

    if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		index = hba->dev_info.wb_dedicated_lu;
    else
        index = 0;

    ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
                      QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST,
                      index, 0, &lifetime);
	if (ret) {
		UFS_LOGE("bWriteBoosterBufferLifeTimeEst read failed %d\n", ret);
		return false;
	}

	if (lifetime == UFS_WB_EXCEED_LIFETIME) {
		UFS_LOGE("WB buf lifetime is exhausted 0x%02X\n", lifetime);
		return false;
	}

	UFS_LOGI("WB buf lifetime is 0x%02X\n", lifetime);

	return true;
}

static void ufshcd_wb_probe(struct ufs_hba *hba, const u8 *desc_buf)
{
    struct ufs_device_info *dev_info = &hba->dev_info;
	u8 lun;
	u32 d_lu_wb_buf_alloc;
    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
    zx_status_t status;
    u8* desc_buf_temp;
    int err = 0;
    ufs_iobuf_t iob = {
        .size = UFS_TEMP_BUF_SIZE,
        .align = ufs->cache_line_size,
        .flags = IO_BUFFER_RW | IO_BUFFER_CONTIG,
    };

	if (!ufshcd_is_wb_allowed(hba))
		return;

    status = ufs_alloc_iobuf(ufs, &iob);
    ZX_ASSERT(status == ZX_OK);
    desc_buf_temp = iob.virt;

    if (!(desc_buf[DEVICE_DESC_PARAM_EXT_FEAT_SUP + 2] & 0x1)){
        UFS_LOGE("WB not supported\n");
        goto wb_disabled;
    }

    if (!dev_info || !dev_info->max_lu_supported) {
		UFS_LOGE("Max General LU supported by UFS isn't initialized\n");
        ufs_free_iobuf(&iob);
		return;
	}
	/*
	 * WB may be supported but not configured while provisioning. The spec
	 * says, in dedicated wb buffer mode, a max of 1 lun would have wb
	 * buffer configured.
	 */
	dev_info->wb_buffer_type = desc_buf[DEVICE_DESC_PARAM_TW_BUF_TYPE];

	dev_info->b_presrv_uspc_en =
		desc_buf[DEVICE_DESC_PARAM_TW_BUF_USER_REDUCED];

    UFS_LOGE("wb_buffer_type:%d,b_presrv_uspc_en:%d\n",dev_info->wb_buffer_type,dev_info->b_presrv_uspc_en);
	if (dev_info->wb_buffer_type == WB_BUF_MODE_SHARED) {
        dev_info->wb_buf_au = (desc_buf[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU] << 24) |
                           (desc_buf[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU + 1] << 16) |
                           (desc_buf[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU + 2] << 5) |
                           (desc_buf[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU + 3]);
		if (!dev_info->wb_buf_au) {
            UFS_LOGE("WB shared buffer allocation units is zero\n");
			goto wb_disabled;
        }
	} else {
		for (lun = 0; lun < UFS_UPIU_MAX_WB_LUN_ID; lun++) {
			d_lu_wb_buf_alloc = 0;
	        if(!(lun == UFS_UPIU_RPMB_WLUN || (lun < dev_info->max_lu_supported)))
                continue;

            err = hba->read_descriptor(hba, QUERY_DESC_IDN_UNIT, lun, 0,
                (u8 *)desc_buf_temp, ufs_query_desc_max_size[QUERY_DESC_IDN_UNIT]);
            if (err) {
                UFS_LOGE("[UFS] err: failed reading GEOMETRY Desc. err = %d\n", err);
                ufs_free_iobuf(&iob);
                return;
            }
            d_lu_wb_buf_alloc = (desc_buf_temp[UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS] << 24) |
                           (desc_buf_temp[UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS + 1] << 16) |
                           (desc_buf_temp[UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS + 2] << 5) |
                           (desc_buf_temp[UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS + 3]);

			if (d_lu_wb_buf_alloc) {
                UFS_LOGI("lun:%d: d_lu_wb_buf_alloc find\n", lun);
                dev_info->wb_buf_au = d_lu_wb_buf_alloc;
				dev_info->wb_dedicated_lu = lun;
				break;
			}
		}

		if (!d_lu_wb_buf_alloc)
			goto wb_disabled;
	}

	if (!ufshcd_is_wb_buf_lifetime_available(hba))
		goto wb_disabled;

    ufs_free_iobuf(&iob);
    return;
wb_disabled:
    ufs_free_iobuf(&iob);
	hba->wb_support = false;
}

static void ufshcd_wb_force_disable(struct ufs_hba *hba)
{
	if (ufshcd_is_wb_buf_flush_allowed(hba))
		ufshcd_wb_toggle_buf_flush(hba, false);

	ufshcd_wb_toggle_buf_flush_during_h8(hba, false);
	ufshcd_wb_toggle(hba, false);
	hba->wb_support = false;

	UFS_LOGI("WB force disabled\n");
}

static bool ufshcd_wb_presrv_usrspc_keep_vcc_on(struct ufs_hba *hba,
						u32 avail_buf)
{
	u32 cur_buf;
	int ret;
	u8 index;

    if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		index = hba->dev_info.wb_dedicated_lu;
    else
        index = 0;

    ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
                      QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE,
                      index, 0, &cur_buf);
	if (ret) {
		UFS_LOGE("dCurWriteBoosterBufferSize read failed %d\n", ret);
		return false;
	}

	if (!cur_buf) {
		UFS_LOGI("dCurWBBuf: %d WB disabled until free-space is available\n",
			 cur_buf);
		return false;
	}
	/* Let it continue to flush when available buffer exceeds threshold */
	return avail_buf < 4; //40%
}

bool ufshcd_wb_need_flush(struct ufs_hba *hba)
{
	int ret;
	u32 avail_buf;
	u8 index;

	if (!ufshcd_is_wb_allowed(hba))
		return false;

	if (!ufshcd_is_wb_buf_lifetime_available(hba)) {
		ufshcd_wb_force_disable(hba);
		return false;
	}

    if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		index = hba->dev_info.wb_dedicated_lu;
    else
        index = 0;

    ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
                      QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE,
                      index, 0, &avail_buf);
	if (ret) {
		UFS_LOGE("dAvailableWriteBoosterBufferSize read failed %d\n", ret);
		return false;
	}

	if (!hba->dev_info.b_presrv_uspc_en)
		return avail_buf <= 1;

	return ufshcd_wb_presrv_usrspc_keep_vcc_on(hba, avail_buf);
}

static int ufshcd_device_geo_params_init(struct ufs_hba *hba)
{
	int err;
	u8 *desc_buf;
    struct ufs_device_info *card_data;
    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
    zx_status_t status;
    ufs_iobuf_t iob = {
        .size = UFS_TEMP_BUF_SIZE,
        .align = ufs->cache_line_size,
        .flags = IO_BUFFER_RW | IO_BUFFER_CONTIG,
    };

    status = ufs_alloc_iobuf(ufs, &iob);
    ZX_ASSERT(status == ZX_OK);
    desc_buf = iob.virt;
    card_data = &hba->dev_info;

    err = hba->read_descriptor(hba, QUERY_DESC_IDN_GEOMETRY, 0, 0,
        (u8 *)desc_buf, ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);
    if (err) {
        UFS_LOGE("[UFS] err: failed reading GEOMETRY Desc. err = %d\n", err);
        ufs_free_iobuf(&iob);
        return -1;
    }

	if (desc_buf[GEOMETRY_DESC_PARAM_MAX_NUM_LUN] == 1)
		hba->dev_info.max_lu_supported = 32;
	else if (desc_buf[GEOMETRY_DESC_PARAM_MAX_NUM_LUN] == 0)
		hba->dev_info.max_lu_supported = 8;

	ufs_free_iobuf(&iob);
	return err;
}

/* Need add error handle */
static int ufshcd_transfer_rsp_status(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    int ret = UFS_ERR_NONE;
    u8 ocs, cnt;
    u32 value;
    u16 resp;

    /* invalid UTRD cache */
    ufs_dma_unmap(lrbp->utr_descriptor_ptr,
                  sizeof(struct utp_transfer_req_desc), DMA_BIDIRECTIONAL);
    ocs = ufshcd_get_tr_ocs(lrbp);

    if (ocs != OCS_SUCCESS) {
        value = ufshcd_readl(hba, REG_UFS_MTK_OCS_ERR_STATUS);
        UFS_LOGE("[UFS] err: OCS err  = %x, T:%d status reg = %x\n",
            ocs, lrbp->task_tag, value);

        for (cnt = 0; g_ufs_ocs_error_str[cnt].err != 0xFF; cnt++) {
            if (value & (1 << cnt))
                UFS_LOGE("[UFS] err: %s\n", g_ufs_ocs_error_str[cnt].name);
        }

        ret = UFS_ERR_OCS_ERROR;
        goto out;
    }

    ufs_dma_unmap(lrbp->ucd_rsp_ptr, sizeof(struct utp_upiu_rsp), DMA_FROM_DEVICE);

    if (lrbp->command_type == UTP_CMD_TYPE_DEV_MANAGE) {
        return ufshcd_dev_cmd_completion(hba, lrbp);
    } else {
        resp = ufshcd_get_rsp_upiu_result(lrbp->ucd_rsp_ptr);
        if (resp & MASK_TASK_RESPONSE) {
            value = ufshcd_get_req_rsp(lrbp->ucd_rsp_ptr);
            if (value == UPIU_TRANSACTION_RESPONSE) {
                ufs_mtk_dump_asc_ascq(hba, lrbp->ucd_rsp_ptr->sr.sense_data[12],
                    lrbp->ucd_rsp_ptr->sr.sense_data[13]);
                    UFS_LOGE("[UFS] Sense Data(all): ");
                    ufs_util_dump_config_table_hex(lrbp->ucd_rsp_ptr->sr.sense_data,
                        18, 18, 0);
            }

            /* sense key is UNIT_ATTENTION, will retry */
            if ((lrbp->ucd_rsp_ptr->sr.sense_data[2] & 0xF) == UNIT_ATTENTION)
                ret = UFS_ERR_UNIT_ATTENTION_RETRY;
            else {
                ret = UFS_ERR_TASK_RESP_ERROR;
                UFS_LOGE("[UFS] err: task response error = %x\n",
                    (resp & MASK_TASK_RESPONSE) >> 8);
            }
            goto out;
        }
    }
out:
    if (lrbp->cmd)
        lrbp->cmd = NULL;

    UFS_LOGD("[UFS] info: CMD done\n");

    return ret;
}

static int ufshcd_transfer_req_compl(struct ufs_hba *hba)
{
    struct ufshcd_lrb *lrbp;
    unsigned long completed_reqs;
    u32 tr_doorbell;
    int ret, i;

    mutex_lock(&hba->outstanding_lock);
    tr_doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
    completed_reqs = ~tr_doorbell & hba->outstanding_reqs;
    hba->outstanding_reqs &= ~completed_reqs;
    mutex_unlock(&hba->outstanding_lock);

    if (completed_reqs) {
        for (i = 0; i < hba->nutrs; i++) {
            if (completed_reqs & (1UL << i)) {
                lrbp = &hba->lrb[i];
                if (lrbp->sync_cmd) {
                    ufshcd_signal_event(hba, lrbp);
                    continue;
                }
                if (lrbp->command_type != UTP_CMD_TYPE_DEV_MANAGE &&
                    lrbp->bop == NULL) {
                    UFS_LOGE("[UFS] skip completion with null bop: tag=%d lun=0x%x cmd_type=%u sync_cmd=%d exp_len=%lu completed=0x%lx outstanding=0x%lx cmd=%p\n",
                             lrbp->task_tag, lrbp->lun, lrbp->command_type,
                             lrbp->sync_cmd, (unsigned long)lrbp->exp_len,
                             completed_reqs, hba->outstanding_reqs, lrbp->cmd);
                    ufshcd_put_tag(hba, lrbp->task_tag);
                    continue;
                }
                ret = ufshcd_transfer_rsp_status(hba, lrbp);
                if (lrbp->command_type != UTP_CMD_TYPE_DEV_MANAGE) {
                    if (ret) {
                        lrbp->bop->completion_cb(lrbp->bop, ZX_ERR_BAD_STATE);
                    } else {
                        ufshcd_complete_bop(hba, lrbp);
                    }
                }
                ufshcd_put_tag(hba, lrbp->task_tag);
            }
        }
    }

    return completed_reqs != 0;
}

static inline bool test_bit(u8 nr, const u32 *addr)
{
    return ((*addr >> nr) & 1) ? 1 : 0;
}

static void ufshcd_dump_phy_err(struct ufs_hba *hba)
{
    u32 reg;

    /* PHY layer error */
    reg = ufs_readl(hba, REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
    if (reg) {
        UFS_LOGE("[UFS] Host UIC Error Code PHY Adapter Layer: 0x%x\n", reg);

        if (test_bit(0, &reg))
            UFS_LOGE("[UFS] PHY error on Lane 0\n");
        if (test_bit(1, &reg))
            UFS_LOGE("[UFS] PHY error on Lane 1\n");
        if (test_bit(2, &reg))
            UFS_LOGE("[UFS] PHY error on Lane 2\n");
        if (test_bit(3, &reg))
            UFS_LOGE("[UFS] PHY error on Lane 3\n");
        if (test_bit(4, &reg)) {
            UFS_LOGE("[UFS] Generic PHY Adapter Error.\n");
            UFS_LOGE("[UFS] This should be the LINERESET indication\n");
        }
    }
}

static void ufshcd_dump_data_link_err(struct ufs_hba *hba)
{
    u32 reg;

    /* Data link layer error */
    reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_DATA_LINK_LAYER);
    if (reg) {
        UFS_LOGE("[UFS] Host UIC Error Code Data Link Layer: 0x%x\n", reg);
        if (test_bit(0, &reg))
            UFS_LOGE("[UFS] NAC_RECEIVED\n");
        if (test_bit(1, &reg))
            UFS_LOGE("[UFS] TCx_REPLAY_TIMER_EXPIRED\n");
        if (test_bit(2, &reg))
            UFS_LOGE("[UFS] AFCx_REQUEST_TIMER_EXPIRED\n");
        if (test_bit(3, &reg))
            UFS_LOGE("[UFS] FCx_PROTECTION_TIMER_EXPIRED\n");
        if (test_bit(4, &reg))
            UFS_LOGE("[UFS] CRC_ERROR\n");
        if (test_bit(5, &reg))
            UFS_LOGE("[UFS] RX_BUFFER_OVERFLOW\n");
        if (test_bit(6, &reg))
            UFS_LOGE("[UFS] MAX_FRAME_LENGTH_EXCEEDEDn");
        if (test_bit(7, &reg))
            UFS_LOGE("[UFS] WRONG_SEQUENCE_NUMBER\n");
        if (test_bit(8, &reg))
            UFS_LOGE("[UFS] AFC_FRAME_SYNTAX_ERROR\n");
        if (test_bit(9, &reg))
            UFS_LOGE("[UFS] NAC_FRAME_SYNTAX_ERROR\n");
        if (test_bit(10, &reg))
            UFS_LOGE("[UFS] EOF_SYNTAX_ERROR\n");
        if (test_bit(11, &reg))
            UFS_LOGE("[UFS] FRAME_SYNTAX_ERROR\n");
        if (test_bit(12, &reg))
            UFS_LOGE("[UFS] BAD_CTRL_SYMBOL_TYPE\n");
        if (test_bit(13, &reg))
            UFS_LOGE("[UFS] PA_INIT_ERROR (FATAL ERROR)\n");
        if (test_bit(14, &reg))
            UFS_LOGE("[UFS] PA_ERROR_IND_RECEIVED\n");
        if (test_bit(15, &reg))
            UFS_LOGE("[UFS] PA_INIT (3.0 FATAL ERROR)\n");
    }
}

static void ufshcd_dump_uic_error(struct ufs_hba *hba)
{
    u32 reg;

    ufshcd_dump_phy_err(hba);

    ufshcd_dump_data_link_err(hba);

    /* Netwrok layer error */
    reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_NETWORK_LAYER);
    UFS_LOGE("[UFS] Host UIC Error Code Network Layer: 0x%x\n", reg);

    /* Transport layer error */
    reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_TRANSPORT_LAYER);
    UFS_LOGE("[UFS] Host UIC Error Code Transport Layer: 0x%x\n", reg);

    /* DME error */
    reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_DME);
    UFS_LOGE("[UFS] Host UIC Error Code: 0x%x\n", reg);
}

/* Need add error handle */
#if 0
static int ufshcd_check_errors(struct ufs_hba *hba, u32 intr_status)
{
    UFS_LOGE("[UFS] intr_status: 0x%x\n", intr_status);

    if (intr_status & UIC_ERROR)
        ufshcd_dump_uic_error(hba);

    ufshcd_writel(hba, UFSHCD_ERROR_MASK, REG_INTERRUPT_STATUS);

    return 0;
}
#endif

static void ufshcd_uic_cmd_compl(struct ufs_hba *hba, u32 intr_status)
{
    if ((intr_status & UIC_COMMAND_COMPL) && hba->active_uic_cmd) {
        hba->active_uic_cmd->argument2 |=
            (ufshcd_readl(hba, REG_UIC_COMMAND_ARG_2) & MASK_UIC_COMMAND_RESULT);
        hba->active_uic_cmd->argument3 = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_3);
    }
}

/**
 * ufshcd_sl_intr - Interrupt service routine
 */
static bool ufshcd_sl_intr(struct ufs_hba *hba, u32 intr_status)
{
    bool retval = false;

    /* May not be used */
    if (intr_status & UFSHCD_UIC_MASK) {
        ufshcd_uic_cmd_compl(hba, intr_status);
    }

    if (intr_status & UFSHCD_ERROR_MASK) {
        retval |= ufshcd_check_errors(hba, intr_status);
    }

#if 0
    if (intr_status & UTP_TASK_REQ_COMPL)
        retval |= ufshcd_tmc_handler(hba);
#endif

    if (intr_status & UTP_TRANSFER_REQ_COMPL) {
        retval |= ufshcd_transfer_req_compl(hba);
    }

    return retval;
}

void ufshcd_irq_handler(struct ufs_hba* hba)
{
    int retries = hba->nutrs;
    u32 intr_status, enabled_intr_status = 0;

    intr_status = ufs_readl(hba, REG_INTERRUPT_STATUS);

    while (intr_status && retries--) {
        enabled_intr_status = intr_status & ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
        ufshcd_writel(hba, intr_status & (~UFSHCD_UIC_MASK), REG_INTERRUPT_STATUS);
        //ufshcd_writel(hba, intr_status, REG_INTERRUPT_STATUS);
        if (enabled_intr_status)
            ufshcd_sl_intr(hba, enabled_intr_status);

        intr_status = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
    }
}

static inline void ufshcd_get_host_capabilities(struct ufs_hba *hba)
{
    /* for AIO driver, only 1 task is enough */
    hba->nutrs = UFS_MAX_NUTRS;
    /* hba->nutmrs = UFS_MAX_NUTMRS; */
}

/**
 * ufshcd_memory_alloc - allocate memory for hba memory space data structures
 * @hba: per adapter instance
 *
 * 1. Allocate DMA memory for Command Descriptor array
 *    Each command descriptor consist of Command UPIU, Response UPIU and PRDT
 * 2. Allocate DMA memory for UTP Transfer Request Descriptor List (UTRDL).
 * 3. Allocate DMA memory for UTP Task Management Request Descriptor List
 *    (UTMRDL)
 * 4. Allocate memory for local reference block(lrb).
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_memory_alloc(struct ufs_hba *hba)
{
    /*
     * Note. UFSHCI 2.1 requires 1024-byte alignment for UTRD (UTP Transfer Request Descriptor)
     *
     * SW tricks:
     *  1. Fill-in UFS_HCI_REG_UTRLBA a 1024-byte alignment address to assume the 1st UTRD entry
     *     (task ID = 0) is in that address.
     *  2. Since every UTRD entry is fixed to 32 bytes, now we can use our real UTRD base address
     *     by using "its" task ID.
     */

    hba->active_tr_tag = (u8)(((unsigned long)hba->utrdl_base_addr & UFS_1KB_MASK) /
        sizeof(struct utp_transfer_req_desc));

    /* Allocate memory for local reference block */
    hba->lrb = malloc(sizeof(struct ufshcd_lrb) * UFS_MAX_NUTRS);
    /* malloc fail */
    UFS_ASSERT(hba->lrb);

    return 0;
}

static void ufshcd_host_memory_configure(struct ufs_hba *hba)
{
    int i;
    int cmd_desc_size;
    u16 prdt_offset;
    u16 response_offset;
    uintptr_t cmd_desc_element_addr;
    uintptr_t cmd_desc_dma_addr;
    struct utp_transfer_req_desc *utrdlp;
    struct utp_transfer_cmd_desc *cmd_descp;

    cmd_descp = hba->ucdl_base_addr;
    utrdlp = hba->utrdl_base_addr;

    prdt_offset     = offsetof(struct utp_transfer_cmd_desc, prd_table);
    response_offset = offsetof(struct utp_transfer_cmd_desc, response_upiu);

    cmd_desc_dma_addr = hba->ucdl_dma_addr;
    cmd_desc_size     = sizeof(struct utp_transfer_cmd_desc);

    for (i = 0; i < hba->nutrs; i++) {
        cmd_desc_element_addr =
            (cmd_desc_dma_addr + (cmd_desc_size * i));
        utrdlp[i].command_desc_base_addr_hi = (u32)(upper_32_bits(cmd_desc_element_addr));
        utrdlp[i].command_desc_base_addr_lo = (u32)(lower_32_bits(cmd_desc_element_addr));

        utrdlp[i].response_upiu_length = (u16)(ALIGNED_UPIU_SIZE >> 2);
        utrdlp[i].prd_table_offset     = (u16)((prdt_offset >> 2));
        utrdlp[i].response_upiu_offset = (u16)((response_offset >> 2));

        hba->lrb[i].ucd_prdt_ptr = (struct ufshcd_sg_entry *)cmd_descp[i].prd_table;
        hba->lrb[i].ucd_req_ptr = (struct utp_upiu_req *)(cmd_descp + i);
        hba->lrb[i].ucd_rsp_ptr = (struct utp_upiu_rsp *)cmd_descp[i].response_upiu;
        hba->lrb[i].utr_descriptor_ptr = (utrdlp + i);
    }
}

/**
 * ufshcd_enable_intr - enable interrupts
 * @hba: per adapter instance
 * @intrs: interrupt bits
 */
static void ufshcd_enable_intr(struct ufs_hba *hba, u32 intrs)
{
    u32 set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);

    set |= intrs;

    ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
}

static int ufshcd_hba_enable(struct ufs_hba *hba, bool reenable)
{
    int retry;

    UFS_LOGI("starting hba enable reenable: %d\n", reenable);
    u32 val = CONTROLLER_ENABLE;
    if ((ufshcd_readl(hba, REG_CONTROLLER_ENABLE) & 0x1) == 1) {
        ufshcd_writel(hba, CONTROLLER_DISABLE,  REG_CONTROLLER_ENABLE);
        usleep(5000);
    }

    /*
     * Reset device after hba disabled
     * This is to make sure host TX is idle
     * when resetting device.
     */
    if (reenable == false)
        ufs_reset_device(hba);

#ifdef UFS_HW_CRYPT_EN
    ufshcd_init_crypto(hba);

    if(ufshcd_crypto_enable(hba, reenable) == false)
        UFS_LOGE("[UFS] Crypto enable failed\n");
    else
        val |= CRYPTO_GENERAL_ENABLE;
#endif

    ufshcd_writel(hba, val, REG_CONTROLLER_ENABLE);
    usleep(1000);
    retry = 20;

    while ((ufshcd_readl(hba, REG_CONTROLLER_ENABLE) & val) == 0) {
        if (retry-- == 0) {
            UFS_LOGE("Controller enable failed\n");
            return -1;
        }
        usleep(5000);
    }
    /* enable UIC related interrupts */
    ufshcd_enable_intr(hba, UFSHCD_UIC_MASK);
    UFS_LOGI("Finish hba enable reenable: %d\n", reenable);

    return 0;
}

static int
__ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd)
{
    if (!(ufshcd_readl(hba, REG_CONTROLLER_STATUS) & UIC_COMMAND_READY)) {
        UFS_LOGW("Controller not ready to accept UIC commands\n");
        return -1;
    }

    UFS_LOGD("[UFS] info: UCMD:%x,%x,%x,%x\n", uic_cmd->command, uic_cmd->argument1,
        uic_cmd->argument2, uic_cmd->argument3);

    hba->active_uic_cmd = uic_cmd;

    ufshcd_writel(hba, uic_cmd->argument1, REG_UIC_COMMAND_ARG_1);
    ufshcd_writel(hba, uic_cmd->argument2, REG_UIC_COMMAND_ARG_2);
    ufshcd_writel(hba, uic_cmd->argument3, REG_UIC_COMMAND_ARG_3);

    ufshcd_writel(hba, uic_cmd->command & COMMAND_OPCODE_MASK, REG_UIC_COMMAND);

    return 0;
}

static int ufshcd_wait_for_register(struct ufs_hba *hba, u32 reg, u32 mask,
    u32 val, unsigned long interval_us, unsigned long timeout_ms)
{
    unsigned long elapsed_us = 0;
    int err = 0;

    val &= mask;

    while (val != (ufshcd_readl(hba, reg) & mask)) {

        /* wakeup within 50us of expiry */
        usleep(interval_us);
        elapsed_us += interval_us;

        if (elapsed_us > timeout_ms * 1000) {
            if ((ufshcd_readl(hba, reg) & mask) != val)
                err = -1;
            break;
        }
    }

    return err;
}

static int ufshcd_wait_for_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd)
{
    int ret;

    ret = ufshcd_wait_for_register(hba, REG_INTERRUPT_STATUS,
                                   UIC_COMMAND_COMPL, UIC_COMMAND_COMPL,
                                   100, UIC_CMD_TIMEOUT);
    if (!ret) {
        ufshcd_writel(hba, UIC_COMMAND_COMPL,
                      REG_INTERRUPT_STATUS);
        ufshcd_uic_cmd_compl(hba, UIC_COMMAND_COMPL);
    } else {
        UFS_LOGW("error! ret: %d\n", ret);
    }

    hba->active_uic_cmd = NULL;

    return ret;
}

static int ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd)
{
    int ret;

    ret = __ufshcd_send_uic_cmd(hba, uic_cmd);

    if (!ret)
        ret = ufshcd_wait_for_uic_cmd(hba, uic_cmd);

    return ret;
}

int ufshcd_uic_cmd_run(struct ufs_hba *hba, struct uic_command *cmds, int ncmds)
{
    int i;
    int err = 0;

    for (i = 0; i < ncmds; i++) {

        err = ufshcd_send_uic_cmd(hba, &cmds[i]);

        if (err) {
            UFS_LOGW("fail, cmd: %x, arg1: %x\n",
                cmds->command, cmds->argument1);

            /* send next commands anyway */
        }
    }

    return err;
}

static int ufshcd_dme_link_startup(struct ufs_hba *hba)
{
    struct uic_command uic_cmd = {0};

    uic_cmd.command = UIC_CMD_DME_LINK_STARTUP;

    return ufshcd_send_uic_cmd(hba, &uic_cmd);
}

/**
 * ufshcd_is_device_present - Check if any device connected to
 *                  the host controller
 * @hba: pointer to adapter instance
 *
 * Returns 1 if device present, 0 if no device detected
 */
static inline int ufshcd_is_device_present(struct ufs_hba *hba)
{
    return (ufshcd_readl(hba, REG_CONTROLLER_STATUS) &
            DEVICE_PRESENT) ? 1 : 0;
}

/**
 * ufshcd_get_lists_status - Check UCRDY, UTRLRDY and UTMRLRDY
 * @reg: Register value of host controller status
 *
 * Returns integer, 0 on Success and positive value if failed
 */
static inline int ufshcd_get_lists_status(u32 reg)
{
    /*
     * The mask 0xFF is for the following HCS register bits
     * Bit      Description
     *  0       Device Present
     *  1       UTRLRDY
     *  2       UTMRLRDY
     *  3       UCRDY
     *  4       HEI
     *  5       DEI
     * 6-7      reserved
     */
    return (((reg) & (0xFF)) >> 1) ^ (0x07);
}

/**
 * ufshcd_enable_run_stop_reg - Enable run-stop registers,
 *          When run-stop registers are set to 1, it indicates the
 *          host controller that it can process the requests
 * @hba: per adapter instance
 */
static void ufshcd_enable_run_stop_reg(struct ufs_hba *hba)
{
    ufshcd_writel(hba, UTP_TASK_REQ_LIST_RUN_STOP_BIT,
                  REG_UTP_TASK_REQ_LIST_RUN_STOP);
    ufshcd_writel(hba, UTP_TRANSFER_REQ_LIST_RUN_STOP_BIT,
                  REG_UTP_TRANSFER_REQ_LIST_RUN_STOP);
}

static int ufshcd_make_hba_operational(struct ufs_hba *hba)
{
    int err = 0;
    u32 reg;

    ufshcd_writel(hba, upper_32_bits(hba->utrdl_dma_addr),
                  REG_UTP_TRANSFER_REQ_LIST_BASE_H);
    ufshcd_writel(hba, lower_32_bits(hba->utrdl_dma_addr),
                  REG_UTP_TRANSFER_REQ_LIST_BASE_L);

    ufshcd_enable_intr(hba, UFSHCD_ENABLE_INTRS);

    reg = ufshcd_readl(hba, REG_CONTROLLER_STATUS);
    if ((ufshcd_get_lists_status(reg)) == 0) {
        ufshcd_enable_run_stop_reg(hba);
    } else {
        UFS_LOGW("Host controller not ready to process requests");
        err = -1;
    }
    UFS_LOGI("ufshcd_make_hba_operational done: err = %d\n", err);
    return err;
}

int ufs_enable_unipro_cg(struct ufs_hba *hba, bool enable)
{
    u32 tmp;
    u32 cfg1 = ((1 << RX_SYMBOL_CLK_GATE_EN) | (1 << SYS_CLK_GATE_EN) |
        (1 << TX_CLK_GATE_EN));
    u32 cfg2 = (1 << TX_SYMBOL_CLK_REQ_FORCE);

    hba->dme_get(hba, UIC_ARG_MIB(VENDOR_SAVEPOWERCONTROL), &tmp);
    tmp = (tmp & ~cfg1) | ((enable) ? cfg1 : 0);
    hba->dme_set(hba, UIC_ARG_MIB(VENDOR_SAVEPOWERCONTROL), tmp);

    hba->dme_get(hba, UIC_ARG_MIB(VENDOR_DEBUGCLOCKENABLE), &tmp);
    tmp = (tmp & ~cfg2) | ((enable) ? 0 : cfg2);
    hba->dme_set(hba, UIC_ARG_MIB(VENDOR_DEBUGCLOCKENABLE), tmp);

    return 0;
}

static int ufs_pre_link(struct ufs_hba *hba)
{
    int ret = 0;
    u32 tmp;

    struct uic_command dme_setting_before_link[] = {
        UIC_CMD_DME_SET(PA_LOCALTXLCCENABLE, 0, 0x0), /* set LCC_Enable to 0 */
    };

    if (hba->hci_quirks & UFSHCD_QUIRK_MTK_MPHY_TESTCHIP) {

        /* enable UIC related interrupts */
        ufshcd_enable_intr(hba, UIC_COMMAND_COMPL);

        /* apply setting before link startup */
        ret = ufshcd_uic_cmd_run(hba, dme_setting_before_link,
                                 countof(dme_setting_before_link));

        if (ret)
            UFS_LOGW("dme_setting_before_link fail\n");

        hba->dme_get(hba, UIC_ARG_MIB(VENDOR_AUTOBURSTENDCTRL), &tmp);
        tmp = ((1<<8) | 0x3c);
        hba->dme_set(hba, UIC_ARG_MIB(VENDOR_AUTOBURSTENDCTRL), tmp);
    }

    /* disable deep stall by default */
    hba->dme_get(hba, UIC_ARG_MIB(VENDOR_SAVEPOWERCONTROL), &tmp);
    tmp &= ~(1 << 6);
    hba->dme_set(hba, UIC_ARG_MIB(VENDOR_SAVEPOWERCONTROL), tmp);

    /* disable scrambling by default */
    hba->dme_set(hba, UIC_ARG_MIB(PA_SCRAMBLING), 0);

    /* disable unipro clock gating feature by default */
    ufs_enable_unipro_cg(hba, false);

    return ret;
}

static int ufs_post_link(struct ufs_hba *hba)
{
    int ret = 0;
    u32 tmp;

    struct uic_command dme_setting_after_link[] = {
        UIC_CMD_DME_PEER_SET(PA_LOCALTXLCCENABLE, 0, 0x0), /* device LCC_Enable to 0 */
    };

    if (hba->hci_quirks & UFSHCD_QUIRK_MTK_MPHY_TESTCHIP) {

        /* apply setting after link startup */
        ret = ufshcd_uic_cmd_run(hba, dme_setting_after_link,
                                 countof(dme_setting_after_link));

        if (ret)
            UFS_LOGW("dme_setting_after_link fail\n");

        hba->dme_get(hba, UIC_ARG_MIB(VENDOR_AUTOBURSTENDCTRL), &tmp);
        tmp &= ~(1<<8);
        hba->dme_set(hba, UIC_ARG_MIB(VENDOR_AUTOBURSTENDCTRL), tmp);
    }

    return ret;

}

/**
 * ufshcd_link_startup - Initialize unipro link startup
 * @hba: per adapter instance
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_link_startup(struct ufs_hba *hba)
{
    int ret;
    int retries = DME_LINKSTARTUP_RETRIES;
    //int i = 0;

    UFS_INIT_DBG("\n");
    do {
        ufs_pre_link(hba);
        UFS_INIT_DBG("\n");
        ret = ufshcd_dme_link_startup(hba);
        UFS_INIT_DBG("\n");
        if (ret) {
            UFS_LOGE("link: UIC command fail ret:%d\n", ret);
            goto retry;
        }

        /* check if device is detected by inter-connect layer */
        if (!ufshcd_is_device_present(hba)) {
            UFS_LOGE("link: Device not present\n");
            ret = -1;
            /* directly go through to retry section */
        }
retry:
        /*
         * DME link lost indication is only received when link is up,
         * but we can't be sure if the link is up until link startup
         * succeeds. So reset the local Uni-Pro and try again.
         */
        if (ret) {
            UFS_LOGE("link startup fail, retrying the %d times ...\n",
                DME_LINKSTARTUP_RETRIES - retries + 1);

            if (ufshcd_hba_enable(hba, false)) {
                /* critical error: controller is not working now, keep ret and goto out */
                goto out;
            }
        }
    } while (ret && retries--);

    if (ret)
        /* failed to get the link up... retired */
        goto out;

    /* Include any host controller configuration via UIC commands */
    ret = ufs_post_link(hba);

    if (ret)
        goto out;

    /* Clear UECPA once due to LINERESET has happened during LINK_STARTUP */
    ufshcd_readl(hba, REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
    ret = ufshcd_make_hba_operational(hba);
out:
    if (ret) {
        UFS_LOGE("link startup failed %d\n", ret);
        ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
        ufshcd_print_evt_hist(hba);
    }
    return ret;
}

/**
 * ufshcd_init_pwr_info - setting the POR (power on reset)
 * values in hba power info
 * @hba: per-adapter instance
 */
static void ufshcd_init_pwr_info(struct ufs_hba *hba)
{
    hba->pwr_info.gear_rx = UFS_PWM_G1;
    hba->pwr_info.gear_tx = UFS_PWM_G1;
    hba->pwr_info.lane_rx = 1;
    hba->pwr_info.lane_tx = 1;
    hba->pwr_info.pwr_rx = SLOW_MODE;
    hba->pwr_info.pwr_tx = SLOW_MODE;
    hba->pwr_info.hs_rate = 1;
}

int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = (1UL << ((nr) % (sizeof(unsigned long) * 8)));
	unsigned long old;

	old = *addr;
	*addr = old | mask;

	return (old & mask) != 0;
}

/**
 * ufshcd_get_dev_cmd_tag - Get device management command tag
 * @hba: per-adapter instance
 * @tag: pointer to variable with available slot value
 *
 * Get a free slot and lock it until device management command
 * completes.
 *
 * Returns false if free slot is unavailable for locking, else
 * return true with tag value in @tag.
 */
bool ufshcd_get_free_tag(struct ufs_hba *hba, int *tag_out)
{
    unsigned long tmp;
    int tag;

    if (!tag_out) {
        UFS_LOGW("[UFS] invalid tag_out\n");
        return false;
    }

retry:
    mutex_lock(&hba->free_tag_lock);
    tmp = ~hba->lrb_in_use;
    mutex_unlock(&hba->free_tag_lock);
    tag = find_first_bit(&tmp, hba->nutrs);

    if (tag >= hba->nutrs) {
        if (completion_wait(&hba->tag_release, ZX_TIME_INFINITE) != ZX_OK) {
            UFS_LOGW("[UFS] fail timeout\n");
            return false;
        }
        completion_reset(&hba->tag_release);
        goto retry;
    }

    mutex_lock(&hba->free_tag_lock);
    if (!test_and_set_bit(tag, &hba->lrb_in_use)) {
        mutex_unlock(&hba->free_tag_lock);
    } else {
        mutex_unlock(&hba->free_tag_lock);
        goto retry;
    }

    *tag_out = tag;

    return true;
}

void ufshcd_put_tag(struct ufs_hba *hba, int tag)
{
    mutex_lock(&hba->free_tag_lock);
    hba->lrb_in_use &= ~(1UL << tag);
    mutex_unlock(&hba->free_tag_lock);
    completion_signal(&hba->tag_release);
}

static void ufshcd_prepare_req_desc_hdr(struct ufs_hba *hba,
                                        struct ufshcd_lrb *lrbp,
                                        u32 *upiu_flags, enum dma_data_direction cmd_dir)
{
    struct utp_transfer_req_desc *req_desc = lrbp->utr_descriptor_ptr;
    u32 dword_0;

    if (cmd_dir == DMA_FROM_DEVICE) {
        dword_0 = UTP_DEVICE_TO_HOST;
        *upiu_flags = UPIU_CMD_FLAGS_READ;
    } else if (cmd_dir == DMA_TO_DEVICE) {
        dword_0 = UTP_HOST_TO_DEVICE;
        *upiu_flags = UPIU_CMD_FLAGS_WRITE;
    } else {
        dword_0 = UTP_NO_DATA_TRANSFER;
        *upiu_flags = UPIU_CMD_FLAGS_NONE;
    }

    dword_0 |= 1 << UPIU_COMMAND_TYPE_OFFSET;

    if (lrbp->intr_cmd)
        dword_0 |= UTP_REQ_DESC_INT_CMD;
#ifdef UFS_HW_CRYPT_EN
    ufshcd_prepare_req_desc_hdr_crypto(lrbp, &req_desc->header);
#endif
    req_desc->header.dword_2 = (u32)(OCS_INVALID_COMMAND_STATUS);
    req_desc->header.dword_0 = (u32)(dword_0);
}


static void ufshcd_prepare_utp_scsi_cmd_upiu(struct ufshcd_lrb *lrbp, u32 upiu_flags)
{
    struct utp_upiu_req *ucd_req = lrbp->ucd_req_ptr;

    ucd_req->sc.exp_data_transfer_len = __builtin_bswap32(lrbp->cmd->exp_len);

    ucd_req->header.dword_2 = 0;

    ucd_req->header.dword_1 = UPIU_HEADER_DWORD(
                                  UPIU_COMMAND_SET_TYPE_SCSI, 0, 0, 0);
    ucd_req->header.dword_0 = UPIU_HEADER_DWORD(
                                  UPIU_TRANSACTION_COMMAND, upiu_flags,
                                  lrbp->lun, lrbp->task_tag);

    memcpy(ucd_req->sc.cdb, lrbp->cmd->cmd_data,
        (min(lrbp->cmd->cmd_len, MAX_CDB_SIZE)));
}

static void ufshcd_prepare_utp_query_req_upiu(struct ufs_hba *hba,
        struct ufshcd_lrb *lrbp, u32 upiu_flags)
{
    u8 *descp = (u8 *)lrbp->ucd_req_ptr + GENERAL_UPIU_REQUEST_SIZE;
    struct ufs_query *query = &hba->dev_cmd.query;
    u16 len = __builtin_bswap16(query->request.upiu_req.length);
    struct utp_upiu_req *ucd_req = lrbp->ucd_req_ptr;

    ucd_req->header.dword_2 = UPIU_HEADER_DWORD(
                                  0, 0, len >> 8, (u8)len);

    ucd_req->header.dword_1 = UPIU_HEADER_DWORD(
                                  0, query->request.query_func, 0, 0);

    ucd_req->header.dword_0 = UPIU_HEADER_DWORD(
                                  UPIU_TRANSACTION_QUERY_REQ, upiu_flags,
                                  lrbp->lun, lrbp->task_tag);

    /* Copy the Query Request buffer as is */
    memcpy(&ucd_req->qr, &query->request.upiu_req, QUERY_OSF_SIZE);

    /* Copy the Descriptor */
    if (query->request.upiu_req.opcode == UPIU_QUERY_OPCODE_WRITE_DESC) {
        memcpy(descp, query->descriptor, len);
        /*
         * ufshcd_send_command() only flushes length of sizeof(struct utp_upiu_req).
         * For Write Descriptor, we may need to flush more data range.
         */
        ufs_dma_map(descp, len, DMA_TO_DEVICE);
    }

}

static int ufshcd_compose_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    u32 upiu_flags;
    int ret = 0;

    switch (lrbp->command_type) {
    case UTP_CMD_TYPE_SCSI:
        if (lrbp->cmd) {
            /* prepare transfer request descriptor */
            ufshcd_prepare_req_desc_hdr(hba, lrbp, &upiu_flags, lrbp->cmd->dir);

            /* prepare COMMAND UPIU */
            ufshcd_prepare_utp_scsi_cmd_upiu(lrbp, upiu_flags);
        } else {
            ret = -1;
        }
        break;
    case UTP_CMD_TYPE_DEV_MANAGE:
        ufshcd_prepare_req_desc_hdr(hba, lrbp, &upiu_flags, DMA_NONE);

        if (hba->dev_cmd.type == DEV_CMD_TYPE_QUERY)
            ufshcd_prepare_utp_query_req_upiu(hba, lrbp, upiu_flags);
        else if (hba->dev_cmd.type == DEV_CMD_TYPE_NOP) {
            memset(lrbp->ucd_req_ptr, 0, sizeof(struct utp_upiu_req));
            lrbp->ucd_req_ptr->header.dword_0 = UPIU_HEADER_DWORD(UPIU_TRANSACTION_NOP_OUT,
                0, 0, lrbp->task_tag);

        } else
            ret = -1;

        break;
    default:
        ret = -1;
        UFS_LOGW("unknown command type: 0x%x\n", lrbp->command_type);
        break;
    } /* end of switch */

    return ret;
}

static int ufshcd_compose_dev_cmd(struct ufs_hba *hba,
                                  struct ufshcd_lrb *lrbp, enum dev_cmd_type cmd_type, int tag)
{
    lrbp->cmd = NULL;
    lrbp->task_tag = tag;
    lrbp->lun = 0; /* device management cmd is not specific to any LUN */
    lrbp->command_type = UTP_CMD_TYPE_DEV_MANAGE;
    lrbp->intr_cmd = true; /* No interrupt aggregation */
    hba->dev_cmd.type = cmd_type;

    return ufshcd_compose_upiu(hba, lrbp);
}

/**
 * ufshcd_send_command - Send SCSI or device management commands
 * @hba: per adapter instance
 * @task_tag: Task tag of the command
 */
static inline void ufshcd_send_command(struct ufs_hba *hba, unsigned int task_tag)
{
    struct ufshcd_lrb *lrbp = &hba->lrb[task_tag];

    /* flush COMMAND UPIU */
    ufs_dma_map(lrbp->ucd_req_ptr, sizeof(struct utp_upiu_req),
        DMA_TO_DEVICE);

    /* invalidate RESPONSE UPIU */
    ufs_dma_map(lrbp->ucd_rsp_ptr, ALIGNED_UPIU_SIZE,
        DMA_FROM_DEVICE);

    /* flush PRDT */
    u16 prdt_len = lrbp->utr_descriptor_ptr->prd_table_length;
    if (prdt_len > 0) {
        ufs_dma_map(lrbp->ucd_prdt_ptr,
            sizeof(struct ufs_sg_entry) * prdt_len, DMA_TO_DEVICE);
    }
    /* invalidate transfer request descriptor (UTRD) */
    ufs_dma_map(lrbp->utr_descriptor_ptr,
        sizeof(struct utp_transfer_req_desc), DMA_BIDIRECTIONAL);

    mutex_lock(&hba->outstanding_lock);
    hba->outstanding_reqs |= (1UL << task_tag);
    ufshcd_writel(hba, 1U << task_tag, REG_UTP_TRANSFER_REQ_DOOR_BELL);
    mutex_unlock(&hba->outstanding_lock);
}

/**
 * ufshcd_exec_dev_cmd - API for sending device management requests
 * @hba - UFS hba
 * @cmd_type - specifies the type (NOP, Query...)
 * @timeout - time in seconds
 *
 * NOTE: Since there is only one available tag for device management commands,
 * it is expected you hold the hba->dev_cmd.lock mutex.
 */
static int ufshcd_exec_dev_cmd(struct ufs_hba *hba,
                               enum dev_cmd_type cmd_type, u32 timeout)
{
    struct ufshcd_lrb *lrbp;
    int err;
    int tag;

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGW("[UFS] No free tag available\n");
        return -1;;
    }

    lrbp = &hba->lrb[tag];
    lrbp->sync_cmd = true;

    err = ufshcd_compose_dev_cmd(hba, lrbp, cmd_type, tag);
    if (err)
        goto out_put_tag;

    ufshcd_send_command(hba, tag);
    err = ufshcd_wait_command(hba, lrbp, timeout);

out_put_tag:

    ufshcd_put_tag(hba, tag);

    return err;
}

static inline void ufshcd_init_query(struct ufs_hba *hba,
    struct ufs_query_req **request, struct ufs_query_res **response,
    enum query_opcode opcode, u8 idn, u8 index, u8 selector)
{
    *request = &hba->dev_cmd.query.request;
    *response = &hba->dev_cmd.query.response;
    memset(*request, 0, sizeof(struct ufs_query_req));
    memset(*response, 0, sizeof(struct ufs_query_res));
    (*request)->upiu_req.opcode = opcode;
    (*request)->upiu_req.idn = idn;
    (*request)->upiu_req.index = index;
    (*request)->upiu_req.selector = selector;
}

/**
 * ufshcd_complete_dev_init() - checks device readiness
 * hba: per-adapter instance
 *
 * Set fDeviceInit flag and poll until device toggles it.
 */
static int ufshcd_complete_dev_init(struct ufs_hba *hba)
{
    int err = 0;
    bool flag_res = 1;
    zx_time_t start_tick;

    err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG,
            QUERY_FLAG_IDN_FDEVICEINIT,0, NULL);
    if (err) {
        UFS_LOGW("setting fDeviceInit flag failed with error %d\n", err);
        goto out;
    }

    // get timeout tick
    start_tick = zx_clock_get(ZX_CLOCK_MONOTONIC);

    /* poll for max. UFS_FDEVICEINIT_TIMEOUT_US(5s) for fDeviceInit flag to clear */
    do {
        err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_READ_FLAG,
                QUERY_FLAG_IDN_FDEVICEINIT,0, &flag_res);
    } while (!err && flag_res &&
        ((zx_clock_get(ZX_CLOCK_MONOTONIC) - start_tick) < UFS_FDEVICEINIT_TIMEOUT_US));

    if (err)
        UFS_LOGW("reading fDeviceInit flag failed with error %d\n", err);
    else if (flag_res) {
        UFS_LOGW("fDeviceInit was not cleared by the device\n");
        err = UFS_ERR_FDEVICEINIT_NOT_CLR;
    }

out:
    return err;
}

static int dump_unipro_params(struct ufs_hba *hba)
{
    int ret = 0;
    u32 granularity, peer_granularity;
    u32 pa_tactivate, peer_pa_tactivate;
    u32 pa_h8time, peer_pa_h8time = 0;

    ret += hba->dme_get(hba, UIC_ARG_MIB(PA_GRANULARITY), &granularity);
    ret += hba->dme_peer_get(hba, UIC_ARG_MIB(PA_GRANULARITY), &peer_granularity);

    ret += hba->dme_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &pa_tactivate);
    ret += hba->dme_peer_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &peer_pa_tactivate);

    ret += hba->dme_get(hba, UIC_ARG_MIB(PA_HIBERN8TIME), &pa_h8time);
    ret += hba->dme_peer_get(hba, UIC_ARG_MIB(PA_HIBERN8TIME), &peer_pa_h8time);

    UFS_LOGE("[UFS] unipro_params: granularity=0x%x\n", granularity);
    UFS_LOGE("[UFS] unipro_params: peer_granularity=0x%x\n", peer_granularity);
    UFS_LOGE("[UFS] unipro_params: pa_tactivate=0x%x\n", pa_tactivate);
    UFS_LOGE("[UFS] unipro_params: peer_pa_tactivate=0x%x\n", peer_pa_tactivate);
    UFS_LOGE("[UFS] unipro_params: pa_h8time=0x%x\n", pa_h8time);
    UFS_LOGE("[UFS] unipro_params: peer_pa_h8time=0x%x\n", peer_pa_h8time);

    if (ret)
        UFS_LOGE("unipro_params: dump with error (%d)\n", ret);
    return ret;
}

static int ufshcd_quirk_tune_host_pa_tactivate(struct ufs_hba *hba)
{
    int ret = 0;
    u32 granularity, peer_granularity;
    u32 pa_tactivate, peer_pa_tactivate;
    u32 pa_tactivate_us, peer_pa_tactivate_us;
    u8 gran_to_us_table[] = {1, 4, 8, 16, 32, 100};

    ret = hba->dme_get(hba, UIC_ARG_MIB(PA_GRANULARITY), &granularity);
    if (ret)
        goto out;

    ret = hba->dme_peer_get(hba, UIC_ARG_MIB(PA_GRANULARITY), &peer_granularity);
    if (ret)
        goto out;

    if ((granularity < PA_GRANULARITY_MIN_VAL) ||
        (granularity > PA_GRANULARITY_MAX_VAL)) {
        UFS_LOGE("invalid host PA_GRANULARITY %d\n",
            granularity);
        return -1;
    }

    if ((peer_granularity < PA_GRANULARITY_MIN_VAL) ||
        (peer_granularity > PA_GRANULARITY_MAX_VAL)) {
        UFS_LOGE("invalid device PA_GRANULARITY %d\n",
            peer_granularity);
        return -1;
    }

    ret = hba->dme_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &pa_tactivate);
    if (ret)
        goto out;

    ret = hba->dme_peer_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &peer_pa_tactivate);
    if (ret)
        goto out;

    pa_tactivate_us = pa_tactivate * gran_to_us_table[granularity - 1];
    peer_pa_tactivate_us = peer_pa_tactivate *
                 gran_to_us_table[peer_granularity - 1];

    if (pa_tactivate_us > peer_pa_tactivate_us) {
        u32 new_peer_pa_tactivate;

        new_peer_pa_tactivate = pa_tactivate_us /
                      gran_to_us_table[peer_granularity - 1];

        new_peer_pa_tactivate++;
        ret = hba->dme_peer_set(hba, UIC_ARG_MIB(PA_TACTIVATE),
                      new_peer_pa_tactivate);
    }

out:
    return ret;
}

/* tune unipro pa parapmer for hibern8 */
static void ufshcd_tune_unipro_params(struct ufs_hba *hba)
{
    if (hba->dev_info.wmanufacturerid == UFS_VENDOR_SAMSUNG) {
        hba->dme_set(hba, UIC_ARG_MIB(PA_TACTIVATE), 6);
        hba->dme_set(hba, UIC_ARG_MIB(PA_HIBERN8TIME), 10);
    }
    if (hba->dev_quirks & UFS_DEVICE_QUIRK_PA_TACTIVATE)
        /* set 1ms timeout for PA_TACTIVATE */
        hba->dme_set(hba, UIC_ARG_MIB(PA_TACTIVATE), 10);

    if (hba->dev_quirks & UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE)
        ufshcd_quirk_tune_host_pa_tactivate(hba);

    dump_unipro_params(hba);
}

int ufshcd_dme_get_attr(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val, u8 peer)
{
    int ret;
    struct uic_command uic_cmd = {0};

    uic_cmd.argument1 = attr_sel;
    uic_cmd.command = peer ? UIC_CMD_DME_PEER_GET : UIC_CMD_DME_GET;

    ret = ufshcd_send_uic_cmd(hba, &uic_cmd);

    if (mib_val && !ret)
        *mib_val = uic_cmd.argument3;

    if (ret)
        UFS_LOGW("attr-id 0x%x error code %d\n", UIC_GET_ATTR_ID(attr_sel), ret);

    return ret;
}

/**
 * ufshcd_dme_set_attr - UIC command for DME_SET, DME_PEER_SET
 * @hba: per adapter instance
 * @attr_sel: uic command argument1
 * @attr_set: attribute set type as uic command argument2
 * @mib_val: setting value as uic command argument3
 * @peer: indicate whether peer or local
 *
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_dme_set_attr(struct ufs_hba *hba, u32 attr_sel, u8 attr_set, u32 mib_val, u8 peer)
{
    struct uic_command uic_cmd = {0};
    int ret;

    uic_cmd.command = peer ? UIC_CMD_DME_PEER_SET : UIC_CMD_DME_SET;
    uic_cmd.argument1 = attr_sel;
    uic_cmd.argument2 = UIC_ARG_ATTR_TYPE(attr_set);
    uic_cmd.argument3 = mib_val;

    ret = ufshcd_send_uic_cmd(hba, &uic_cmd);

    if (ret)
        UFS_LOGE("[UFS] err: attr-id 0x%x val 0x%x error code %d (peer %d)\n",
            UIC_GET_ATTR_ID(attr_sel), mib_val, ret, peer);

    return ret;
}

/**
 * ufshcd_get_max_pwr_mode - reads the max power mode negotiated with device
 * @hba: per-adapter instance
 */
static int ufshcd_get_max_pwr_mode(struct ufs_hba *hba)
{
    struct ufs_pa_layer_attr *pwr_info = &hba->max_pwr_info.info;

    if (hba->max_pwr_info.is_valid)
        return 0;

    pwr_info->pwr_tx = FAST_MODE;
    pwr_info->pwr_rx = FAST_MODE;
    pwr_info->hs_rate = PA_HS_MODE_B;

    /* Get the connected lane count */
    hba->dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDRXDATALANES),
                 &pwr_info->lane_rx);
    /* mtk debug */
    UFS_LOGI("[UFS] info: lane_rx: %d\n", pwr_info->lane_rx);

    hba->dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES),
                 &pwr_info->lane_tx);
    /* mtk debug */
    UFS_LOGI("[UFS] info: lane_tx: %d\n", pwr_info->lane_tx);

    if (!pwr_info->lane_rx || !pwr_info->lane_tx) {
        UFS_LOGW("invalid connected lanes value. rx=%d, tx=%d\n",
                     pwr_info->lane_rx,
                     pwr_info->lane_tx);
        return -1;
    }

    /*
     * First, get the maximum gears of HS speed.
     * If a zero value, it means there is no HSGEAR capability.
     * Then, get the maximum gears of PWM speed.
     */
    hba->dme_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR), &pwr_info->gear_rx);

    if (!pwr_info->gear_rx) {
        hba->dme_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR), &pwr_info->gear_rx);
        if (!pwr_info->gear_rx) {
            UFS_LOGW("invalid max pwm rx gear read = %d\n", pwr_info->gear_rx);
            return -1;
        }
        pwr_info->pwr_rx = SLOWAUTO_MODE;
    } else
        /* mtk debug */
        UFS_LOGI("[UFS] info: gear_rx: %d\n", pwr_info->gear_rx);
    hba->dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR),
                      &pwr_info->gear_tx);

    if (!pwr_info->gear_tx) {
        hba->dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
                          &pwr_info->gear_tx);
        if (!pwr_info->gear_tx) {
            UFS_LOGW("invalid max pwm tx gear read = %d\n", pwr_info->gear_tx);
            return -1;
        }
        pwr_info->pwr_tx = SLOWAUTO_MODE;
    } else
        /* mtk debug */
        UFS_LOGI("[UFS] info: gear_tx: %d\n", pwr_info->gear_tx);

    hba->max_pwr_info.is_valid = true;

    return 0;
}

/**
 * ufshcd_get_upmcrs - Get the power mode change request status
 * @hba: Pointer to adapter instance
 *
 * This function gets the UPMCRS field of HCS register
 * Returns value of UPMCRS field
 */
static inline u8 ufshcd_get_upmcrs(struct ufs_hba *hba)
{
    return (ufshcd_readl(hba, REG_CONTROLLER_STATUS) >> 8) & 0x7;
}

static int ufshcd_uic_pwr_ctrl(struct ufs_hba *hba, struct uic_command *cmd)
{
    u8 status;
    int ret;

    ret = __ufshcd_send_uic_cmd(hba, cmd);

    if (ret) {
        UFS_LOGW("pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n", cmd->command,
            cmd->argument3, ret);
        goto out;
    }

    ret = ufshcd_wait_for_uic_cmd(hba, cmd);

    if (ret) {
        UFS_LOGW("pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n", cmd->command,
            cmd->argument3, ret);
        goto out;
    }

    ret = ufshcd_wait_for_register(hba, REG_INTERRUPT_STATUS,
                                   UIC_POWER_MODE, UIC_POWER_MODE,
                                   100, UIC_CMD_TIMEOUT);
    if (ret) {
        UFS_LOGE("[UFS] err: wait UIC_POWER_MODE interrupt timeout\n");
        goto out;
    }

    ufshcd_writel(hba, UIC_POWER_MODE, REG_INTERRUPT_STATUS);

    status = ufshcd_get_upmcrs(hba);

    if (status != PWR_LOCAL) {
        UFS_LOGE("[UFS] err: pwr ctrl cmd 0x%0x failed, host umpcrs:0x%x\n",
            cmd->command, status);
        ret = (status != PWR_OK) ? status : -1;
    }

out:
    if (ret) {
		ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
        ufshcd_print_evt_hist(hba);
	}
    return ret;
}

static int ufshcd_uic_change_pwr_mode(struct ufs_hba *hba, u8 mode)
{
    int ret;
    struct uic_command uic_cmd = {0};

    uic_cmd.argument1 = UIC_ARG_MIB(PA_PWRMODE);
    uic_cmd.command = UIC_CMD_DME_SET;
    uic_cmd.argument3 = mode;
    ret = ufshcd_uic_pwr_ctrl(hba, &uic_cmd);

    return ret;
}

static int ufshcd_uic_h8_enter_ctrl(struct ufs_hba *hba, struct uic_command *cmd)
{
	u8 status;
	int ret;

    UFS_LOGD("start cmd 0x%x with mode 0x%x\n",
			     cmd->command, cmd->argument3);

	ret = __ufshcd_send_uic_cmd(hba, cmd);

	if (ret) {
		UFS_LOGE("pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n",
			     cmd->command, cmd->argument3, ret);
		goto out;
	}

	ret = ufshcd_wait_for_uic_cmd(hba, cmd);

	if (ret) {
		UFS_LOGE("pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n",
			     cmd->command, cmd->argument3, ret);
		goto out;
	}

	ret = ufshcd_wait_for_register(hba, REG_INTERRUPT_STATUS,
				       UIC_HIBERNATE_ENTER, UIC_HIBERNATE_ENTER,
				       100, UIC_CMD_TIMEOUT);
	if (ret) {
		UFS_LOGE("[UFS] err: wait UIC_POWER_MODE interrupt timeout\n");
		goto out;
	}

	ufshcd_writel(hba, UIC_HIBERNATE_ENTER, REG_INTERRUPT_STATUS);

	status = ufshcd_get_upmcrs(hba);

	if (status != PWR_LOCAL) {
		UFS_LOGE("[UFS] err: pwr ctrl cmd 0x%0x failed, host umpcrs:0x%x\n",
			     cmd->command, status);
		ret = (status != PWR_OK) ? status : -1;
	}

out:
    UFS_LOGD("Done cmd 0x%x with mode 0x%x ret:%d\n",
			     cmd->command, cmd->argument3, ret);
	return ret;
}

static int ufshcd_uic_h8_exit_ctrl(struct ufs_hba *hba, struct uic_command *cmd)
{
	u8 status;
	int ret;

    UFS_LOGD("start cmd 0x%x with mode 0x%x\n",
			     cmd->command, cmd->argument3);

	ret = __ufshcd_send_uic_cmd(hba, cmd);

	if (ret) {
		UFS_LOGD("pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n",
			     cmd->command, cmd->argument3, ret);
		goto out;
	}

	ret = ufshcd_wait_for_uic_cmd(hba, cmd);

	if (ret) {
		UFS_LOGD("pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n",
			     cmd->command, cmd->argument3, ret);
		goto out;
	}

	ret = ufshcd_wait_for_register(hba, REG_INTERRUPT_STATUS,
				       UIC_HIBERNATE_EXIT, UIC_HIBERNATE_EXIT,
				       100, UIC_CMD_TIMEOUT);
	if (ret) {
		UFS_LOGE("[UFS] err: wait UIC_POWER_MODE interrupt timeout\n");
		goto out;
	}

	ufshcd_writel(hba, UIC_HIBERNATE_EXIT, REG_INTERRUPT_STATUS);

	status = ufshcd_get_upmcrs(hba);

	if (status != PWR_LOCAL) {
		UFS_LOGE("[UFS] err: pwr ctrl cmd 0x%0x failed, host umpcrs:0x%x\n",
			     cmd->command, status);
		ret = (status != PWR_OK) ? status : -1;
	}

out:
    UFS_LOGD("Done cmd 0x%x with mode 0x%x ret:%d\n",
			     cmd->command, cmd->argument3, ret);
	return ret;
}

int ufshcd_uic_hibern8_enter(struct ufs_hba *hba)
{
	int ret;
	struct uic_command uic_cmd = {0};

	uic_cmd.command = UIC_CMD_DME_HIBER_ENTER;
	ret = ufshcd_uic_h8_enter_ctrl(hba, &uic_cmd);
    UFS_LOGW("[UFS] ufshcd_uic_hibern8_enter done\n");

	return ret;
}

int ufshcd_uic_hibern8_exit(struct ufs_hba *hba)
{
	int ret;
	struct uic_command uic_cmd = {0};

	uic_cmd.command = UIC_CMD_DME_HIBER_EXIT;
	ret = ufshcd_uic_h8_exit_ctrl(hba, &uic_cmd);
	UFS_LOGI("[UFS] ufshcd_uic_hibern8_exit done\n");

	return ret;
}

static int ufs_mtk_setup_ref_clk(struct ufs_hba *hba, bool on)
{
	u32 value;
	unsigned int timeout_us = REFCLK_REQ_TIMEOUT_US;

    if (hba->ref_clk_enabled == on)
        return 0;

    ufs_mtk_ref_clk_notify(on, 0);

	if (on) {
		ufshcd_writel(hba, REFCLK_REQUEST, REG_UFS_REFCLK_CTRL);
	} else {
		if (hba->ref_clk_gating_wait_us)
			udelay(hba->ref_clk_gating_wait_us);
		ufshcd_writel(hba, REFCLK_RELEASE, REG_UFS_REFCLK_CTRL);
	}

	while (timeout_us > 0) {
		value = ufshcd_readl(hba, REG_UFS_REFCLK_CTRL);
		if (((value & REFCLK_ACK) >> 1) == (value & REFCLK_REQUEST))
			break;
        udelay(100);
        timeout_us -= 100;
	}

    if (timeout_us == 0) {
        UFS_LOGE("Timeout waiting for REFCLK_ACK value:0x%x\n", value);
        if (!on)
            hba->ref_clk_enabled = false;
        ufs_mtk_ref_clk_notify(hba->ref_clk_enabled, 1);
        return -1;
    }

    hba->ref_clk_enabled = on;
	if (on && hba->ref_clk_ungating_wait_us)
		udelay(hba->ref_clk_ungating_wait_us);

    UFS_LOGI("ufs: ref_clk_enabled = %d, gating_us = %d on:%d\n",
        hba->ref_clk_enabled, hba->ref_clk_gating_wait_us, on);

    ufs_mtk_ref_clk_notify(on, 1);
    return 0;
}

static void ufs_mtk_setup_ref_clk_wait_us(struct ufs_hba *hba, u16 gating_us)
{
    u32 attr = 0;

    hba->ref_clk_gating_wait_us = gating_us;
    if (!ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
            QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME, 0, 0, &attr) && attr)
        hba->ref_clk_gating_wait_us = (u16)attr;

    hba->ref_clk_ungating_wait_us = REFCLK_DEFAULT_WAIT_US;
}

static void ufs_mtk_init_ref_clk_wait_us(struct ufs_hba *hba)
{
    switch (hba->dev_info.wmanufacturerid) {
    case UFS_VENDOR_SAMSUNG:
        ufs_mtk_setup_ref_clk_wait_us(hba, 1);
        break;
    case UFS_VENDOR_SKHYNIX:
        ufs_mtk_setup_ref_clk_wait_us(hba, 30);
        break;
    case UFS_VENDOR_TOSHIBA:
        ufs_mtk_setup_ref_clk_wait_us(hba, 100);
        break;
    default:
        ufs_mtk_setup_ref_clk_wait_us(hba, REFCLK_DEFAULT_WAIT_US);
        break;
    }
}

/* ---- SMC-based UFS clock control via ATF ---- *
 * clk SMC ABI :
 *   CLK_SMC_CMD(vm_id, type, clk_id, opc) =
 *       (vm_id<<24) | (type<<21) | (clk_id<<3) | opc
 *   CLK_ID(pvd, idx)                     = (pvd<<9) | idx
 * ATF returns status in x2 (mapped to zx_smc_result_t::arg2).
 */
/* vm_id: hypervisor VM (new id, must be aligned with ATF) */
enum {
	ANDROID_VM = 0,
	TBOX_VM,
	SOS_VM,
	ATF_VM,
	LK_VM,
	HYP_VM,
	VM_COUNT,
};

enum clk_smc_op_type {
	CLK_EN = 0,
	CLK_DIS = 1,
	CLK_PREPARE = 2,
	CLK_UNPREPARE = 3,
	SCPSYS_EN = 4,
	SCPSYS_DIS = 5,
	INFRA_HYP_ON = 6,
	INFRA_HYP_OFF = 7,
};

enum clk_cmd_type {
	CLK_OPS = 1,
	SCPSYS_OPS = 2,
};

#define CLK_ID(pvd, idx) \
    ((((uint64_t)(pvd)) << 9) | ((uint64_t)(idx) & 0x1FFULL))
#define CLK_SMC_CMD(vm_id, type, clk_id, opc) \
    ((((uint64_t)(vm_id)) << 24) | (((uint64_t)(type)) << 21) | \
     (((uint64_t)(clk_id)) << 3)  | ((uint64_t)(opc)))

/* Low-level SMC issuance for one clock op. */
static zx_status_t ufs_clk_smc(uint32_t pvd, uint32_t id, uint32_t op,
                               const char* name) {
    zx_smc_parameters_t p = {
        .func_id = MTK_SIP_KERNEL_CLK_CONTROL,
        .arg1 = CLK_SMC_CMD(HYP_VM, CLK_OPS,
                                CLK_ID(pvd, id), op),
        .arg2 = 0, .arg3 = 0, .arg4 = 0, .arg5 = 0, .arg6 = 0,
        .client_id = 0, .secure_os_id = 0,
    };
    zx_smc_result_t r = {0};

    UFS_LOGD("arg1:0x%lx\n", (unsigned long)p.arg1);
    zx_status_t st = zx_smc_call(get_root_resource(), &p, &r);
    if (st != ZX_OK) {
        UFS_LOGE("clk SMC failed: %s pvd=%u id=%u op=%u st=%d\n",
                 name, pvd, id, op, st);
        return st;
    }
    /* ATF returns status in arg2 (see ATF clkmgr handler spec: ret = ares.a2) */
    if ((int64_t)r.arg2 != 0) {
        UFS_LOGE("clk SMC ATF err: %s pvd=%u id=%u op=%u a0=0x%lx a2=%ld\n",
                 name, pvd, id, op, (unsigned long)r.arg0, (long)r.arg2);
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

/* Generic per-clock APIs. Callers pass (pvd_id, clk_id, name). */
static zx_status_t clock_enable(uint32_t pvd_id, uint32_t clk_id,
                                const char* name) {
    return ufs_clk_smc(pvd_id, clk_id, CLK_EN, name);
}

static zx_status_t clock_disable(uint32_t pvd_id, uint32_t clk_id,
                                 const char* name) {
    return ufs_clk_smc(pvd_id, clk_id, CLK_DIS, name);
}

static zx_status_t ufs_clk_enable_all(void)
{
    const struct ufs_clk_desc* ufs_clk_list = NULL;
    size_t clk_count = ufs_mtk_get_clk_list(&ufs_clk_list);

    UFS_LOGD("clk_count:%ld\n", clk_count);
    for (size_t i = 0; i < clk_count; i++) {
        const struct ufs_clk_desc* c = &ufs_clk_list[i];
        zx_status_t st = clock_enable(c->pvd, c->id, c->name);
        if (st != ZX_OK) {
            UFS_LOGE("Failed to enable ufs clk st:%d\n", st);
            /* roll back already-enabled clocks in reverse order */
            for (size_t j = i; j-- > 0;) {
                const struct ufs_clk_desc* p = &ufs_clk_list[j];
                (void)clock_disable(p->pvd, p->id, p->name);
            }
            return st;
        }
    }
    return ZX_OK;
}

static void ufs_clk_disable_all(void)
{
    const struct ufs_clk_desc* ufs_clk_list = NULL;
    size_t clk_count = ufs_mtk_get_clk_list(&ufs_clk_list);

    UFS_LOGD("clk_count:%ld\n", clk_count);
    for (size_t i = clk_count; i-- > 0;) {
        const struct ufs_clk_desc* c = &ufs_clk_list[i];
        (void)clock_disable(c->pvd, c->id, c->name);
    }
}

static int ufshcd_setup_ufs_clocks(struct ufs_hba *hba, bool on)
{
    if (on)
        return ufs_clk_enable_all();

    ufs_clk_disable_all();
    return 0;
}

static int ufshcd_dme_copy_attr(struct ufs_hba *hba,
                                u32 attr_sel_dst, u8 peer_dst,
                                u32 attr_sel_src, u8 peer_src)
{
    int ret;
    u32 mib_val;

    ret = ufshcd_dme_get_attr(hba, attr_sel_src, &mib_val, peer_src);
    if (ret)
        return ret;

    return ufshcd_dme_set_attr(hba, attr_sel_dst, ATTR_SET_NOR,
                               mib_val, peer_dst);
}

static int ufshcd_setup_userdata(struct ufs_hba *hba)
{
    /* Set PAPowerModeUserData[0~5] */
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA0), 0x1fff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA1), 0xffff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA2), 0x7fff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA3), 0x1fff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA4), 0xffff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA5), 0x7fff);

    return 0;
}

static u32 get_termination(u32 pwr_mode)
{
    switch (pwr_mode) {
    case FASTAUTO_MODE:
    case FAST_MODE:
        return 1;
    case SLOWAUTO_MODE:
    case SLOW_MODE:
        return 0;
    }

    return 0;
}
static int pwr_mode_change(struct ufs_hba *hba,
                           u32 tx_pwr_mode, u32 rx_pwr_mode,
                           u32 tx_gear, u32 rx_gear,
                           u32 tx_lanes, u32 rx_lanes, u32 hs_series)
{
    u32 tx_term = get_termination(tx_pwr_mode);
    u32 rx_term = get_termination(rx_pwr_mode);
    struct uic_command cmds[7];
    int ret;

    ret = ufshcd_dme_copy_attr(hba,
                               UIC_ARG_MIB(PA_TXTRAILINGCLOCKS), DME_LOCAL,
                               UIC_ARG_MIB(PA_MINRXTRAILINGCLOCKS), DME_PEER);
    if (ret)
        return ret;

    ret = ufshcd_dme_copy_attr(hba,
                               UIC_ARG_MIB(PA_TXTRAILINGCLOCKS), DME_PEER,
                               UIC_ARG_MIB(PA_MINRXTRAILINGCLOCKS), DME_LOCAL);
    if (ret)
        return ret;

    cmds[0] = UIC_CMD_DME_SET(PA_TXTERMINATION, 0, tx_term);
    cmds[1] = UIC_CMD_DME_SET(PA_RXTERMINATION, 0, rx_term);

    cmds[2] = UIC_CMD_DME_SET(PA_TXGEAR, 0, tx_gear);
    cmds[3] = UIC_CMD_DME_SET(PA_RXGEAR, 0, rx_gear);

    cmds[4] = UIC_CMD_DME_SET(PA_ACTIVETXDATALANES, 0, tx_lanes);
    cmds[5] = UIC_CMD_DME_SET(PA_ACTIVERXDATALANES, 0, rx_lanes);

    cmds[6] = UIC_CMD_DME_SET(PA_HSSERIES, 0, hs_series);

    return ufshcd_uic_cmd_run(hba, cmds, countof(cmds));
}

int ufshcd_legacy_pwr_mode_change(struct ufs_hba *hba, u32 tx_pwr_mode,
                                  u32 rx_pwr_mode, u32 tx_gear, u32 rx_gear,
                                  u32 hs_series, u32 tx_lanes, u32 rx_lanes)
{
    int err;

    err = ufshcd_setup_userdata(hba);
    if (err)
        return err;

    err = pwr_mode_change(hba, tx_pwr_mode, rx_pwr_mode,
                          tx_gear, rx_gear, tx_lanes, rx_lanes, hs_series);
    if (err)
        return err;

    err = ufshcd_uic_change_pwr_mode(hba, rx_pwr_mode << 4 | tx_pwr_mode);

    return err;
}

static int ufs_pre_pwr_change(struct ufs_hba *hba, struct ufs_pa_layer_attr *desired,
    struct ufs_pa_layer_attr *final)
{
    if (hba->dev_info.wmanufacturerid == 0) {
        UFS_LOGW("bug: manufacturer_id shall not be 0");
        return -1;
    }

    /* fill-in desired gear for different environment */
    /* make sure device capability */
    final->pwr_rx = UFS_DEV_DEFAULT_PWR_RX;
    final->pwr_tx = UFS_DEV_DEFAULT_PWR_TX;
    final->hs_rate = UFS_DEV_DEFAULT_HS_RATE;

    if (final->pwr_rx == SLOW_MODE || final->pwr_rx == SLOWAUTO_MODE) {
        final->gear_rx = min(UFS_DEV_MAX_GEAR_RX, 4);
        final->gear_tx = min(UFS_DEV_MAX_GEAR_TX, 4);
    } else {
        final->gear_rx = min(UFS_DEV_MAX_GEAR_RX, desired->gear_rx);
        final->gear_tx = min(UFS_DEV_MAX_GEAR_TX, desired->gear_tx);

        final->gear_rx = min(final->gear_rx, final->gear_tx);
        final->gear_tx = final->gear_rx;

        UFS_LOGI("[UFS] info: RX HS-G%d, setting is %d\n",
            final->gear_rx, UFS_DEV_MAX_GEAR_RX);
        UFS_LOGI("[UFS] info: TX HS-G%d, setting is %d\n",
            final->gear_tx, UFS_DEV_MAX_GEAR_TX);
    }

    final->lane_rx = min(UFS_DEV_MAX_LANE_RX, desired->lane_rx);
    final->lane_tx = min(UFS_DEV_MAX_LANE_TX, desired->lane_tx);

    if (final->pwr_rx == SLOW_MODE || final->pwr_rx == SLOWAUTO_MODE)
        UFS_LOGI("[UFS] info: PWM-G%d, %d lane\n",
                final->gear_rx, final->lane_rx);
    else
        UFS_LOGI("[UFS] info: HS-G%d-%d, %d lane\n",
                final->gear_rx, final->hs_rate, final->lane_rx);

    /* Set PAPowerModeUserData[0~5] = 0xffff, default is 0 */
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA0), 0x1fff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA1), 0xffff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA2), 0x7fff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA3), 0x1fff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA4), 0xffff);
    hba->dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA5), 0x7fff);

    hba->dme_set(hba, UIC_ARG_MIB(DME_LocalFC0ProtectionTimeOutVal),
        DL_FC0ProtectionTimeOutVal_Default);
    hba->dme_set(hba, UIC_ARG_MIB(DME_LocalTC0ReplayTimeOutVal),
        DL_TC0ReplayTimeOutVal_Default);
    hba->dme_set(hba, UIC_ARG_MIB(DME_LocalAFC0ReqTimeOutVal),
        DL_AFC0ReqTimeOutVal_Default);

    return 0;
}

static bool ufshcd_pmc_via_fastauto(struct ufs_hba *hba, u32 version,
    struct ufs_pa_layer_attr *dev_req_params)
{
    if (version < UNIPRO_VER_2_00)
        return false;

    if (dev_req_params->hs_rate == hba->pwr_info.hs_rate)
        return false;

    if ((dev_req_params->pwr_tx != FAST_MODE) &&
        (dev_req_params->gear_tx < UFS_HS_G4))
        return false;

    if ((dev_req_params->pwr_rx != FAST_MODE) &&
        (dev_req_params->gear_rx < UFS_HS_G4))
        return false;

    if ((dev_req_params->pwr_tx == SLOW_MODE) ||
        (dev_req_params->pwr_rx == SLOW_MODE))
        return false;

    return true;
}

/**
 * ufshcd_config_pwr_mode - configure a new power mode
 * @hba: per-adapter instance
 * @desired_pwr_mode: desired power configuration
 */
int ufshcd_config_pwr_mode(struct ufs_hba *hba,
                           struct ufs_pa_layer_attr *desired_pwr_mode)
{
    struct ufs_pa_layer_attr final_params = { 0 };
    int ret;
    u32 version;

    ufs_pre_pwr_change(hba, desired_pwr_mode, &final_params);

    hba->dme_get(hba, UIC_ARG_MIB(PA_LOCALVERINFO), &version);

    UFS_LOGI("[UFS] unipro info: local=%d\n", version);

    /* UFS4.0 Change to FASTAUTO first for compatible device */
    if (ufshcd_pmc_via_fastauto(hba, version, &final_params)) {
        hba->dme_set(hba, UIC_ARG_MIB(PA_TXHSADAPTTYPE), PA_NO_ADAPT);
        ret = ufshcd_legacy_pwr_mode_change(hba,
                                        FASTAUTO_MODE, FASTAUTO_MODE,
                                        UFS_HS_G1, UFS_HS_G1,
                                        final_params.hs_rate,
                                        final_params.lane_tx, final_params.lane_rx);
        UFS_LOGI("[UFS] Power mode change to HSG1 FASTAUTO: ret=%d\n", ret);
    }

    if (final_params.pwr_rx == FAST_MODE || final_params.pwr_rx == FASTAUTO_MODE) {
        if ((final_params.gear_tx >= 4) && (final_params.gear_rx >= 4))
            hba->dme_set(hba, UIC_ARG_MIB(PA_TXHSADAPTTYPE), PA_INITIAL_ADAPT);
        else
            hba->dme_set(hba, UIC_ARG_MIB(PA_TXHSADAPTTYPE), PA_NO_ADAPT);
    } else {
        hba->dme_set(hba, UIC_ARG_MIB(PA_TXHSADAPTTYPE), PA_NO_ADAPT);
    }

    ret = ufshcd_legacy_pwr_mode_change(hba,
                                        final_params.pwr_tx, final_params.pwr_rx,
                                        final_params.gear_tx, final_params.gear_rx,
                                        final_params.hs_rate,
                                        final_params.lane_tx, final_params.lane_rx);

    return ret;
}

int ufshcd_map_sg(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
    struct ufshcd_sg_entry *prd_table;
    struct ufs_scsi_cmd *cmd;
    int sg_size = UFS_MAX_SIZE_PER_SG_SEGMENT;
    int sg_segments;
    int i;
    u32 residual;
    uintptr_t buf_dma_addr;

    cmd = lrbp->cmd;

    if (cmd->sg_count > 0) {
        uint64_t total_len = 0;
        int prd_count = 0;

        if (!cmd->sg || cmd->exp_len == 0) {
            UFS_LOGE("Err: explicit SG command is invalid sg=%p count=%u len=%u\n",
                     cmd->sg, cmd->sg_count, cmd->exp_len);
            return UFS_ERR_INVALID_ARGS;
        }

        prd_table = (struct ufshcd_sg_entry *)lrbp->ucd_prdt_ptr;

        for (i = 0; i < (int)cmd->sg_count; i++) {
            uintptr_t sg_dma_addr = cmd->sg[i].paddr;
            uint32_t sg_residual = cmd->sg[i].len;

            if (sg_residual == 0 || (sg_dma_addr & (PRDT_DATA_BYTE_COUNT_PAD - 1U)) ||
                (sg_residual & (PRDT_DATA_BYTE_COUNT_PAD - 1U))) {
                UFS_LOGE("Err: invalid explicit SG entry i=%d addr=0x%lx len=%u\n",
                         i, (unsigned long)sg_dma_addr, sg_residual);
                return UFS_ERR_INVALID_ARGS;
            }

            total_len += sg_residual;
            if (total_len > cmd->exp_len) {
                UFS_LOGE("Err: explicit SG total exceeds exp_len total=%llu exp=%u\n",
                         (unsigned long long)total_len, cmd->exp_len);
                return UFS_ERR_INVALID_ARGS;
            }

            while (sg_residual > 0) {
                uint32_t prd_len = sg_residual > (uint32_t)sg_size
                                       ? (uint32_t)sg_size
                                       : sg_residual;

                if (prd_count >= UFS_MAX_SG_SEGMENTS) {
                    UFS_LOGE("Err: explicit SG PRDT overflow count=%d max=%u\n",
                             prd_count, (unsigned int)UFS_MAX_SG_SEGMENTS);
                    return UFS_ERR_INVALID_ARGS;
                }

                prd_table[prd_count].size = prd_len - 1U;
                prd_table[prd_count].base_addr = (u32)(lower_32_bits(sg_dma_addr));
                prd_table[prd_count].upper_addr = (u32)(upper_32_bits(sg_dma_addr));
                prd_table[prd_count].reserved = 0;

                sg_dma_addr += prd_len;
                sg_residual -= prd_len;
                prd_count++;
            }
        }

        if (total_len != cmd->exp_len) {
            UFS_LOGE("Err: explicit SG total mismatch total=%llu exp=%u\n",
                     (unsigned long long)total_len, cmd->exp_len);
            return UFS_ERR_INVALID_ARGS;
        }

        lrbp->utr_descriptor_ptr->prd_table_length = (u16)prd_count;
        return 0;
    }

    if (cmd->exp_len == 0)
        sg_segments = 0;
    else
        sg_segments = ((cmd->exp_len + sg_size - 1) / sg_size);

    if (sg_segments) {
        lrbp->utr_descriptor_ptr->prd_table_length = (u16)((u16) (sg_segments));

        prd_table = (struct ufshcd_sg_entry *)lrbp->ucd_prdt_ptr;

        buf_dma_addr = cmd->paddr;

        residual = cmd->exp_len;

        for (i = 0; i < sg_segments; i++) {
            /* size should be 32-bit aligned, this is forced by spec */
            if (residual > (u32)sg_size)
                prd_table[i].size = (u32)((u32)sg_size - 1);
            else
                prd_table[i].size = (u32)((u32)residual - 1);

            /* addr should be 32-bit aligned, this is forced by spec */
            prd_table[i].base_addr = (u32)(lower_32_bits(buf_dma_addr));
            prd_table[i].upper_addr = (u32)(upper_32_bits(buf_dma_addr));

            buf_dma_addr += sg_size;
            residual -= sg_size;
        }

        // cache operation moved to block_rw
    } else {
        lrbp->utr_descriptor_ptr->prd_table_length = 0;
    }

    return 0;
}

int ufshcd_wait_command(struct ufs_hba *hba, struct ufshcd_lrb *lrbp, u32 timeout_ms)
{
    u8 ocs;
    u16 resp;
    int ret = UFS_ERR_NONE;
    u32 value;
    u32 tag_mask = 1U << lrbp->task_tag;
    u32 tr_doorbell;
    u32 intr_status;
    unsigned long elapsed_ms = 0;

    // TODO:
    // interrupt mode not work.
    if (hba->drv_status & UFS_DRV_STS_DMA_WAIT_INTERRUPT) {
        zx_time_t tmo = ZX_TIME_INFINITE;

        /* for msdc irq support */
        /* Unmask interrupt now and wait */
        //ufs_unmask_interrupt(hba, hba->irq);
        //printf("unmask %s %d\n", __func__, __LINE__);
        //zx_interrupt_unmask(hba->irq_handle, hba->irq);

        if (timeout_ms > 0)
            tmo = zx_deadline_after(ZX_MSEC(timeout_ms));

        zx_status_t status;
        uint32_t observed = 0U;

        status = zx_object_wait_one(hba->irq_event, UFS_IRQ_RECEIVED, tmo, &observed);
        if (status != ZX_OK) {
            UFS_LOGE("[UFS]: failed to get event %dms timeout! (%d)\n",
                   timeout_ms, status);
        }
        if(observed & UFS_IRQ_RECEIVED)
            zx_object_signal(hba->irq_event, UFS_IRQ_RECEIVED, 0);
        /* Mask interrupt */
        //ufs_mask_interrupt(hba, hba->irq);
        //zx_interrupt_mask(hba->irq_handle, hba->irq);
       // printf("mask %s %d\n", __func__, __LINE__);

    } else
        do {
            tr_doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);

            if (!(tag_mask & tr_doorbell)) {
                /* clear_bit(lrbp->task_tag, &hba->outstanding_reqs); */
                hba->outstanding_reqs &= ~(1UL << lrbp->task_tag);
                break;
            }

            usleep(1000);
            elapsed_ms += 1;

            if (elapsed_ms > timeout_ms)
                break;
        } while (1);

    tr_doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);

    if (tag_mask & tr_doorbell) {
        UFS_LOGE("[UFS] err: Query Request timeout.timeout_ms:%d\n", timeout_ms);
        ret = UFS_ERR_TIMEOUT_QUERY_REQUEST;
    }

    if (hba->drv_status & UFS_DRV_STS_DMA_WAIT_INTERRUPT)
        intr_status = hba->irq_status;
    else
        intr_status = ufs_readl(hba, REG_INTERRUPT_STATUS);

    /* check interrupt status if any error happens */
    if (intr_status & UFSHCD_ERROR_MASK) {
        UFS_LOGE("[UFS] ERR! intr_status: 0x%x en:0x%x\n",
            intr_status, ufs_readl(hba, REG_INTERRUPT_ENABLE));
        if (intr_status & UIC_ERROR)
            ufshcd_dump_uic_error(hba);

        ufshcd_writel(hba, UFSHCD_ERROR_MASK, REG_INTERRUPT_STATUS);
    }

    if (ret == UFS_ERR_TIMEOUT_QUERY_REQUEST) {
        //mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
        /* dump host reg */
        //UFS_LOGI("[UFS] dump host reg\n");
        //hexdump((void *)ufs->hci_base, 0xA0);
        goto out;
    }

    // DMA buffer cache operating moved to block_rw
        // if (lrbp->cmd)
        // ufs_dma_unmap((unsigned long)lrbp->cmd->data_buf, lrbp->cmd->exp_len, lrbp->cmd->dir);
    /* dummy, just for pairing with ufs_dma_map(UTRD) and ufs_dma_map(PRDT) */
    /* ufs_dma_unmap((unsigned long)lrbp->ucd_req_ptr, sizeof(struct utp_upiu_req), */
    /* DMA_TO_DEVICE); */
    /* ufs_dma_unmap((unsigned long)hba->lrb->ucd_prdt_ptr, */
    /* sizeof(struct ufs_sg_entry) * UFS_MAX_SG_SEGMENTS, DMA_TO_DEVICE); */

    /* check OCS */

    /* invalid UTRD cache */
    ufs_dma_unmap(lrbp->utr_descriptor_ptr,
                  sizeof(struct utp_transfer_req_desc), DMA_BIDIRECTIONAL);
    ocs = ufshcd_get_tr_ocs(lrbp);

    if (ocs != OCS_SUCCESS) {
        UFS_LOGE("[UFS] err: OCS error = %x, T:%d\n", ocs, lrbp->task_tag);

        {
            u8 cnt;

            value = ufshcd_readl(hba, REG_UFS_MTK_OCS_ERR_STATUS);
            UFS_LOGE("[UFS] err: OCS err status reg = %x\n", value);

            for (cnt = 0; g_ufs_ocs_error_str[cnt].err != 0xFF; cnt++) {
                if (value & (1 << cnt))
                    UFS_LOGE("[UFS] err: %s\n", g_ufs_ocs_error_str[cnt].name);
            }
        }
        ret = UFS_ERR_OCS_ERROR;
        goto out;
    }

    /* check response code */

    /* invalid RESPONSE UPIU cache
     *
     * NOTE: Here we only invalidate length of "strcut utp_upiu_rsp". For Response UPIU with
     *       longer data size (e.g., Read Descriptor), need to invalidate more data.
     *       Please see ufshcd_copy_query_response().
     */
    ufs_dma_unmap(lrbp->ucd_rsp_ptr, sizeof(struct utp_upiu_rsp), DMA_FROM_DEVICE);

    if (lrbp->command_type == UTP_CMD_TYPE_DEV_MANAGE) { /* for device management commands */
        ret = ufshcd_dev_cmd_completion(hba, lrbp);
        goto out;
    } else {                                             /* for SCSI commands */
        resp = ufshcd_get_rsp_upiu_result(lrbp->ucd_rsp_ptr);
        if (resp & MASK_TASK_RESPONSE) {

            value = ufshcd_get_req_rsp(lrbp->ucd_rsp_ptr);
            if (value == UPIU_TRANSACTION_RESPONSE) {
                ufs_mtk_dump_asc_ascq(hba, lrbp->ucd_rsp_ptr->sr.sense_data[12],
                    lrbp->ucd_rsp_ptr->sr.sense_data[13]);
                    UFS_LOGI("[UFS] Sense Data(all): ");
                    ufs_util_dump_config_table_hex(lrbp->ucd_rsp_ptr->sr.sense_data,
                        18, 18, 0);
            }

            /* sense key is UNIT_ATTENTION, will retry */
            if ((lrbp->ucd_rsp_ptr->sr.sense_data[2] & 0xF) == UNIT_ATTENTION)
                ret = UFS_ERR_UNIT_ATTENTION_RETRY;
            else {
                ret = UFS_ERR_TASK_RESP_ERROR;
                UFS_LOGE("[UFS] err: task response error = %x\n",
                    (resp & MASK_TASK_RESPONSE) >> 8);
            }
            goto out;
        }
    }

out:
    if (lrbp->cmd)
        lrbp->cmd = NULL;

    /*
     * Keep sync SCSI tags owned until response handling is fully finished.
     * Releasing too early allows async block requests to reuse the same tag
     * while the caller is still consuming the completion, which can corrupt
     * the shared LRB state.
     */
    if (lrbp->command_type != UTP_CMD_TYPE_DEV_MANAGE)
        ufshcd_put_tag(hba, lrbp->task_tag);

    UFS_LOGD("[UFS] info: CMD done\n");

    return ret;
}

void ufs_crypto_cal_dun(u32 alg_id, u32 lba, u32 *dunl, u32 *dunu)
{
    if (alg_id != UFS_CRYPTO_ALGO_BITLOCKER_AES_CBC) {
        *dunl = lba;
        *dunu = 0;
    } else {                             /* bitlocker dun use byte address */
        *dunl = (lba & 0x7FFFF) << 12;   /* byte address for lower 32 bit */
        *dunu = (lba >> (32-12)) << 12;  /* byte address for higher 32 bit */
    }
}

static void ufshcd_queuecommand_timeout(struct ufs_scsi_cmd *cmd, int *timeout)
{
    if (cmd->cmd_data[0] == UNMAP)
        *timeout = UTP_UNMAP_TIMEOUT_MS;
    else
        *timeout = UTP_TRANSFER_REQ_TIMEOUT_MS;
}

int ufshcd_queuecommand(struct ufs_hba *hba, struct ufs_scsi_cmd *cmd)
{
    struct ufshcd_lrb *lrbp;
    int tag, lun;
    int err = 0;
    u32 blk_cnt;
    u32 lba;
    int timeout = UTP_TRANSFER_REQ_TIMEOUT_MS;

    tag = cmd->tag;
    lun = cmd->lun;

    lba = cmd->cmd_data[5] | (cmd->cmd_data[4] << 8) | (cmd->cmd_data[3] << 16) |
        ((unsigned)cmd->cmd_data[2] << 24);
    blk_cnt = cmd->cmd_data[8] | (cmd->cmd_data[7] << 8);

    UFS_LOGD("[UFS] info: QCMD,L:%x,T:%d,0x%x,A:0x%x,C:%d\n", lun, tag, cmd->cmd_data[0],
        lba, blk_cnt);

    lrbp = &hba->lrb[tag];

    lrbp->cmd = cmd;
    lrbp->task_tag = tag;
    lrbp->lun = lun;
    lrbp->intr_cmd = true;    /* in AIO driver, use interrupt for every commands. */
    lrbp->command_type = UTP_CMD_TYPE_SCSI;
#ifdef UFS_HW_CRYPT_EN
    ufshcd_prepare_lrbp_crypto(lrbp);
#endif
    /* form UPIU before issuing the command */
    ufshcd_compose_upiu(hba, lrbp);
    err = ufshcd_map_sg(hba, lrbp);
    if (err) {
        UFS_LOGE("Err: ufshcd_map_sg_ut fails\n");
        lrbp->cmd = NULL;
        ufshcd_put_tag(hba, lrbp->task_tag);
        goto out;
    }

    /* issue command to the controller */
    ufshcd_send_command(hba, tag);

    /* Setting utp transfer req command timeout according to different cmd */
    ufshcd_queuecommand_timeout(cmd, &timeout);

    /* wait for command completion */
    if (lrbp->sync_cmd)
        return ufshcd_wait_command(hba, lrbp, timeout);

out:
    return err;
}

static int ufs_test_unit_ready_all_device(struct ufs_hba *hba)
{
    int i, tag;
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int ret = UFS_ERR_NONE;

    if (hba->dev_info.num_active_lu == 0) {
        UFS_LOGE("bug: active_num_lu shall not be 0");
        return -1;
    }

    if (!ufshcd_get_free_tag(hba, &tag)) {
        UFS_LOGW("[UFS] No free tag available\n");
        return -1;;
    }

    lrbp = &hba->lrb[tag];
    lrbp->sync_cmd = true;
    /* send test unit ready to each LUN to remove possible UNIT ATTENTION */
    for (i = 0; i < hba->dev_info.num_active_lu; i++) {
        do {
            ufs_scsi_cmd_test_unit_ready(&cmd, tag, i);

            /* check error code if retrun with UNIT ATTENTION */
            ret = ufshcd_queuecommand(hba, &cmd);
        } while (ret);
    }

    /* send test unit ready to WRPMB LUN */
    do {
        ufs_scsi_cmd_test_unit_ready(&cmd, tag, WLUN_RPMB);
        /* check error code if retrun with UNIT ATTENTION */
        ret = ufshcd_queuecommand(hba, &cmd);
    } while (ret);

    hba->drv_status &= ~UFS_DRV_STS_TEST_UNIT_READY_ALL_DEVICE;

    ufshcd_put_tag(hba, tag);

    return 0;
}


int ufshcd_write_ee_control(struct ufs_hba *hba)
{
	int err;

	mutex_lock(&hba->ee_ctrl_mutex);
	err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
				       QUERY_ATTR_IDN_EE_CONTROL, 0, 0,
				       &hba->ee_ctrl_mask);
	mutex_unlock(&hba->ee_ctrl_mutex);
	if (err)
		UFS_LOGE("failed to write ee control %d\n", err);

	return err;
}

int ufshcd_update_ee_control(struct ufs_hba *hba, u16 *mask,
			     const u16 *other_mask, u16 set, u16 clr)
{
	u16 new_mask;
    u32 ee_ctrl_mask;
	int err = 0;

	mutex_lock(&hba->ee_ctrl_mutex);
	new_mask = (*mask & ~clr) | set;
	ee_ctrl_mask = new_mask | *other_mask;
	if (ee_ctrl_mask != hba->ee_ctrl_mask)
        err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
				       QUERY_ATTR_IDN_EE_CONTROL, 0, 0,
				       &ee_ctrl_mask);
	/* Still need to update 'mask' even if 'ee_ctrl_mask' was unchanged */
	if (!err) {
		hba->ee_ctrl_mask = (u16)ee_ctrl_mask;
		*mask = new_mask;
	}
	mutex_unlock(&hba->ee_ctrl_mutex);
	return err;
}

/**
 * ufshcd_disable_ee - disable exception event
 * @hba: per-adapter instance
 * @mask: exception event to disable
 *
 * Disables exception event in the device so that the EVENT_ALERT
 * bit is not set.
 *
 * Return: zero on success, non-zero error value on failure.
 */
static inline int ufshcd_disable_ee(struct ufs_hba *hba, u16 mask)
{
    return ufshcd_update_ee_control(hba, &hba->ee_drv_mask,
					&hba->ee_usr_mask, 0, mask);
}

/**
 * ufshcd_enable_ee - enable exception event
 * @hba: per-adapter instance
 * @mask: exception event to enable
 *
 * Enable corresponding exception event in the device to allow
 * device to alert host in critical scenarios.
 *
 * Return: zero on success, non-zero error value on failure.
 */
static inline int ufshcd_enable_ee(struct ufs_hba *hba, u16 mask)
{
    return ufshcd_update_ee_control(hba, &hba->ee_drv_mask,
					&hba->ee_usr_mask, mask, 0);
}

/**
 * ufshcd_enable_auto_bkops - Allow device managed BKOPS
 * @hba: per-adapter instance
 *
 * Allow device to manage background operations on its own. Enabling
 * this might lead to inconsistent latencies during normal data transfers
 * as the device is allowed to manage its own way of handling background
 * operations.
 *
 * Return: zero on success, non-zero on failure.
 */
static int ufshcd_enable_auto_bkops(struct ufs_hba *hba)
{
	int err = 0;

	if (hba->auto_bkops_enabled)
		goto out;

	err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG,
			QUERY_FLAG_IDN_BKOPS_EN, 0, NULL);
	if (err) {
		UFS_LOGE("failed to enable bkops %d\n", err);
		goto out;
	}

	hba->auto_bkops_enabled = true;

	/* No need of URGENT_BKOPS exception from the device */
	err = ufshcd_disable_ee(hba, MASK_EE_URGENT_BKOPS);
	if (err)
		UFS_LOGE("failed to disable exception event %d\n", err);
out:
	return err;
}

/**
 * ufshcd_disable_auto_bkops - block device in doing background operations
 * @hba: per-adapter instance
 *
 * Disabling background operations improves command response latency but
 * has drawback of device moving into critical state where the device is
 * not-operable. Make sure to call ufshcd_enable_auto_bkops() whenever the
 * host is idle so that BKOPS are managed effectively without any negative
 * impacts.
 *
 * Return: zero on success, non-zero on failure.
 */
static int ufshcd_disable_auto_bkops(struct ufs_hba *hba)
{
	int err = 0;

	if (!hba->auto_bkops_enabled)
		goto out;

	/*
	 * If host assisted BKOPs is to be enabled, make sure
	 * urgent bkops exception is allowed.
	 */
	err = ufshcd_enable_ee(hba, MASK_EE_URGENT_BKOPS);
	if (err) {
		UFS_LOGE("failed to enable exception event %d\n", err);
		goto out;
	}

	err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG,
			QUERY_FLAG_IDN_BKOPS_EN, 0, NULL);
	if (err) {
		UFS_LOGE("failed to disable bkops %d\n", err);
		ufshcd_disable_ee(hba, MASK_EE_URGENT_BKOPS);
		goto out;
	}

	hba->auto_bkops_enabled = false;

    hba->is_urgent_bkops_lvl_checked = false;
out:
	return err;
}

/**
 * ufshcd_force_reset_auto_bkops - force reset auto bkops state
 * @hba: per adapter instance
 *
 * After a device reset the device may toggle the BKOPS_EN flag
 * to default value. The s/w tracking variables should be updated
 * as well. This function would change the auto-bkops state based on
 * UFSHCD_CAP_KEEP_AUTO_BKOPS_ENABLED_EXCEPT_SUSPEND.
 */
static void ufshcd_force_reset_auto_bkops(struct ufs_hba *hba)
{
    UFS_LOGI("Skip bkops\n");
    return;

    /* Default disable */
    hba->auto_bkops_enabled = true;
    hba->ee_ctrl_mask &= ~MASK_EE_URGENT_BKOPS;
    ufshcd_disable_auto_bkops(hba);

	hba->urgent_bkops_lvl = BKOPS_STATUS_PERF_IMPACT;
	hba->is_urgent_bkops_lvl_checked = false;
}

/**
 * ufshcd_bkops_ctrl - control the auto bkops based on current bkops status
 * @hba: per-adapter instance
 *
 * Read the bkops_status from the UFS device and Enable fBackgroundOpsEn
 * flag in the device to permit background operations if the device
 * bkops_status is greater than or equal to the "hba->urgent_bkops_lvl",
 * disable otherwise.
 *
 * Return: 0 for success, non-zero in case of failure.
 *
 * NOTE: Caller of this function can check the "hba->auto_bkops_enabled" flag
 * to know whether auto bkops is enabled or disabled after this function
 * returns control to it.
 */
int ufshcd_bkops_ctrl(struct ufs_hba *hba)
{
	enum bkops_status status = hba->urgent_bkops_lvl;
	u32 curr_status = 0;
	int err;

    err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_BKOPS_STATUS, 0, 0, &curr_status);
	if (err) {
		UFS_LOGE("failed to get BKOPS status %d\n", err);
		goto out;
	} else if (curr_status > BKOPS_STATUS_MAX) {
		UFS_LOGE("invalid BKOPS status %d\n", curr_status);
		err = -UFS_ERR_INVALID_ARGS;
		goto out;
	}

	if (curr_status >= status)
		err = ufshcd_enable_auto_bkops(hba);
	else
		err = ufshcd_disable_auto_bkops(hba);
out:
	return err;
}

static inline int ufshcd_get_ee_status(struct ufs_hba *hba, u32 *status)
{
	return ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_EE_STATUS, 0, 0, status);
}

static void ufshcd_bkops_exception_event_handler(struct ufs_hba *hba)
{
	int err;
	u32 curr_status = 0;

	if (hba->is_urgent_bkops_lvl_checked)
		goto enable_auto_bkops;

    err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_BKOPS_STATUS, 0, 0, &curr_status);
	if (err) {
		UFS_LOGE("failed to get BKOPS status %d\n", err);
		goto out;
	}

	/*
	 * We are seeing that some devices are raising the urgent bkops
	 * exception events even when BKOPS status doesn't indicate performace
	 * impacted or critical. Handle these device by determining their urgent
	 * bkops status at runtime.
	 */
	if (curr_status < BKOPS_STATUS_PERF_IMPACT) {
		UFS_LOGE("device raised urgent BKOPS exception for bkops status %d\n", curr_status);
		/* update the current status as the urgent bkops level */
		hba->urgent_bkops_lvl = curr_status;
		hba->is_urgent_bkops_lvl_checked = true;
	}

enable_auto_bkops:
	err = ufshcd_enable_auto_bkops(hba);
out:
	if (err < 0)
		UFS_LOGE("failed to handle urgent bkops %d\n", err);
}

static void ufshcd_temp_exception_event_handler(struct ufs_hba *hba, u16 status)
{
	u32 value;

	if (ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
				QUERY_ATTR_IDN_CASE_ROUGH_TEMP, 0, 0, &value))
		return;

	UFS_LOGI("exception Tcase %d\n", value - 80);

	//ufs_hwmon_notify_event(hba, status & MASK_EE_URGENT_TEMP);

	/*
	 * A placeholder for the platform vendors to add whatever additional
	 * steps required
	 */
}

/**
 * ufshcd_exception_event_handler - handle exceptions raised by device
 * @work: pointer to work data
 *
 * Read bExceptionEventStatus attribute from the device and handle the
 * exception event accordingly.
 */
//static void ufshcd_exception_event_handler(struct work_struct *work)
static void ufshcd_exception_event_handler(struct ufs_hba *hba)
{
	//struct ufs_hba *hba;
	int err;
	u32 status = 0;
	//hba = container_of(work, struct ufs_hba, eeh_work);

	//ufshcd_scsi_block_requests(hba);
	err = ufshcd_get_ee_status(hba, &status);
	if (err) {
		UFS_LOGE("failed to get exception status %d\n", err);
		goto out;
	}

	if (status & hba->ee_drv_mask & MASK_EE_URGENT_BKOPS)
		ufshcd_bkops_exception_event_handler(hba);

	if (status & hba->ee_drv_mask & MASK_EE_URGENT_TEMP)
		ufshcd_temp_exception_event_handler(hba, status);

	if (status & hba->ee_drv_mask & MASK_EE_HEALTH_CRITICAL) {
		hba->critical_health_count++;
		//sysfs_notify(&hba->dev->kobj, NULL, "critical_health");
	}
#if 0
	if (status & hba->ee_drv_mask & MASK_EE_DEV_LVL_EXCEPTION) {
		atomic_inc(&hba->dev_lvl_exception_count);
		sysfs_notify(&hba->dev->kobj, NULL, "device_lvl_exception_count");
	}
#endif
	//ufs_debugfs_exception_event(hba, status);
out:
	//ufshcd_scsi_unblock_requests(hba);
    UFS_LOGI("err %d status:0x%x\n", err, status);
}

static int ufshcd_probe_hba(struct ufs_hba *hba)
{
    int ret;

    UFS_LOGI("start\n");
    ret = ufshcd_link_startup(hba);
    if (ret)
        goto out;

    UFS_INIT_DBG("\n");
    ufshcd_init_pwr_info(hba);

    ret = hba->nopin_nopout(hba);
#if 0
    int i = 0;
    for(i = 0; i < 48; i++){
        printf("UFSHCI dump %s(offset:%x),value:%x\n",
            enum_reg_map[i].name, enum_reg_map[i].value, ufshcd_readl(hba, enum_reg_map[i].value));
    }
#endif
    if (ret) {
        UFS_LOGE("nopin_nopout fail ret:%d\n", ret);
        goto out;
    }
    UFS_INIT_DBG("\n");

    ret = ufshcd_complete_dev_init(hba);
    if (ret) {
        UFS_LOGE("ufshcd_complete_dev_init fail ret:%d\n", ret);
        goto out;
    }
    UFS_INIT_DBG("\n");

    /* Init check for device descriptor sizes */
    ufshcd_init_desc_sizes(hba);
    ret = ufs_get_device_info(hba);
    if (ret) {
        UFS_LOGE("Failed getting device info. err = %d\n", ret);
        goto out;
    }
    UFS_INIT_DBG("\n");

    /* get quirks according to Device Descritpor contents */
    ufs_advertise_fixup_device(hba);
    ufs_mtk_init_ref_clk_wait_us(hba);
    ufshcd_tune_unipro_params(hba);
    ufs_check_lu_cfg(hba);
    ufs_test_unit_ready_all_device(hba);
    /* ufshcd_force_reset_auto_bkops(hba); */
    UFS_INIT_DBG("\n");

    if (ufshcd_get_max_pwr_mode(hba))
        UFS_LOGE("Failed getting max supported power mode\n");
    else {
        ret = ufshcd_config_pwr_mode(hba, &hba->max_pwr_info.info);
        if (ret) {
            UFS_LOGW("Failed setting power mode, err = %d\n", ret);

            /* ignore power mode change error to let UFS init go through anyway */
            ret = 0;
        }
    }
    UFS_INIT_DBG("\n");
    ufshcd_print_pwr_info(hba);

    /* Enable UFS Write Booster if supported */
	ufshcd_configure_wb(hba);

    ufshcd_force_reset_auto_bkops(hba);

    ufshcd_exception_event_handler(hba);

    UFS_INIT_DBG("\n");

    /* enable auto-hibern8 (10x100ms=1s) for pre-charging reduce ufs power */
    ufs_mtk_auto_hibern8_enable(hba);

out:
    UFS_LOGI("Done ret:%d\n", ret);

    return ret;
}

struct ufs_hba *ufs_get_host(u8 host_id)
{
    UFS_LOGD("Done\n");
    return g_ufs_hba;
}

void ufs_set_host(mtk_ufs_t* ufs)
{
    UFS_LOGD("Done\n");
    g_ufs_hba = &ufs->hba;
}

int ufshcd_init(mtk_ufs_t* ufs)
{
    int err;
    int retry_init_cnt = 0;
    struct ufs_hba* hba = &ufs->hba;

    UFS_LOGI(" start\n");
    UFS_INIT_DBG("ufs:%p\n", ufs);

    /* get hba capabilities */
    ufshcd_get_host_capabilities(hba);
    /* init power, add when need */
    /* init clock, add when need */

    /*init wb*/
    hba->wb_support = true;
    hba->hci_quirks |= UFS_DEVICE_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL;

    hba->eh_flags = 0;
    hba->lrb_in_use = 0;
    hba->eh_event = ZX_HANDLE_INVALID;
    hba->eh_thread_running = false;

    UFS_INIT_DBG("hci_quirks:%x\n", hba->hci_quirks);
#ifdef UFS_HW_CRYPT_EN
    /* Read crypto capabilities */
	err = ufshcd_hba_init_crypto_capabilities(hba);
	if (err) {
		UFS_LOGE("crypto setup failed\n");
        ufshcd_print_evt_hist(hba);
		goto out_error;
	}
#endif
    UFS_INIT_DBG("\n");

    /* allocate memory for hba memory space */
    err = ufshcd_memory_alloc(hba);

    if (err) {
        UFS_LOGW("Memory allocation failed\n");
        goto out_error;
    }
    UFS_INIT_DBG("\n");

    /* Configure LRB */
    ufshcd_host_memory_configure(hba);
    ufs_set_host(ufs);
    err = ufshcd_eh_init(hba);
    if (err) {
        UFS_LOGE("[UFS] err: failed to init eh worker\n");
        goto out_error;
    }
    UFS_INIT_DBG("\n");

    UFS_LOGD("Enable ufs regulator\n");
    ufs_mtk_vreg_ctrl(true);

    UFS_LOGD("Enable ufs clocks\n");
    ufshcd_setup_ufs_clocks(hba, true);

ufshcd_init_reinit_host:
    /* Power on */
    ufs_mtk_mtcmos_ctrl(true);
    /* Host controller enable */
    err = ufshcd_hba_enable(hba, false);
    if (err) {
        UFS_LOGE("[UFS] err: Host controller enable failed\n");
	ufshcd_eh_exit(hba);
        goto out_error;
    }

    UFS_INIT_DBG("\n");
    mtx_init(&hba->free_tag_lock, mtx_plain);
    mtx_init(&hba->outstanding_lock, mtx_plain);
    mtx_init(&hba->bop_bitmap_lock, mtx_plain);
    mtx_init(&hba->ee_ctrl_mutex, mtx_plain);

    completion_reset(&hba->tag_release);

    err = ufshcd_probe_hba(hba);
    if (err == UFS_ERR_NEED_REINIT_HOST) {
        retry_init_cnt++;

        if (retry_init_cnt > 3) {
            UFS_LOGE("[UFS] err: retry number exceeded (reinit host)\n");
	    ufshcd_eh_exit(hba);
            goto out_error;
        }
        ufs_mtk_mtcmos_ctrl(false);
        goto ufshcd_init_reinit_host;
    }
    if (err) {
        UFS_LOGE("[UFS] err: ufshcd_probe_hba failed\n");
        ufshcd_eh_exit(hba);
        goto out_error;
    }

    UFS_INIT_DBG("\n");

    //init irq
    if (!ufshcd_irq_init(hba))
        hba->drv_status |= UFS_DRV_STS_DMA_WAIT_INTERRUPT;

    UFS_LOGI("Done\n");

    return 0;

out_error:
    ufs_mtk_mtcmos_ctrl(false);

    return err;
}

static int ufshcd_query_descriptor(struct ufs_hba *hba,
                                   enum query_opcode opcode, enum desc_idn idn, u8 index,
                                   u8 selector, u8 *desc_buf, u32 *buf_len)
{
    struct ufs_query_req *request = NULL;
    struct ufs_query_res *response = NULL;
    int err;

    UFS_ASSERT(hba);

    if (*buf_len < QUERY_DESC_MIN_SIZE || *buf_len > QUERY_DESC_MAX_SIZE) {
        UFS_LOGW("descriptor buffer size (%d) is out of range\n", *buf_len);
        err = -1;
        goto out;
    }

    if (!desc_buf) {
        UFS_LOGW("descriptor buffer required for opcode 0x%x\n", opcode);
        err = -1;
        goto out;
    }

    ufshcd_init_query(hba, &request, &response, opcode, idn, index, selector);
    request->upiu_req.length = __builtin_bswap16(*buf_len);
    hba->dev_cmd.query.descriptor = desc_buf;

    switch (opcode) {
    case UPIU_QUERY_OPCODE_READ_DESC:
        request->query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQUEST;
        break;

    case UPIU_QUERY_OPCODE_WRITE_DESC:
        request->query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST;
        break;

    default:
        UFS_LOGE("Expected query descriptor opcode but got = 0x%x\n", opcode);
        err = -1;
        goto out;
    }

    err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_QUERY, QUERY_REQ_TIMEOUT);

    if (err) {
        UFS_LOGE("opcode 0x%x for idn %d failed, err = %d\n", opcode, idn, err);
        goto out;
    }

    *buf_len = __builtin_bswap16(response->upiu_res.length);
    hba->dev_cmd.query.descriptor = NULL;

out:
    return err;
}

int ufshcd_query_descriptor_retry(struct ufs_hba *hba,
				  enum query_opcode opcode,
				  enum desc_idn idn, u8 index,
				  u8 selector,
				  u8 *desc_buf, u32 *buf_len)
{
	int err;
	int retries;

	for (retries = QUERY_REQ_RETRIES; retries > 0; retries--) {
		err = ufshcd_query_descriptor(hba, opcode, idn, index,
						selector, desc_buf, buf_len);
		if (!err || err == -UFS_ERR_INVALID_ARGS)
			break;
	}

	return err;
}

static int ufshcd_read_desc_param(struct ufs_hba *hba,
                                  enum desc_idn desc_id,
                                  int desc_index,
                                  u8 selector,
                                  u32 param_offset,
                                  u8 *param_read_buf,
                                  u32 param_size)
{
    u32 desc_len;
    u8 *desc_buf;
    int ret;

    if (desc_id >= QUERY_DESC_IDN_MAX)
        return -1;

    desc_len = ufs_query_desc_max_size[desc_id];

    if (desc_len > param_size)
        desc_len = param_size;
    desc_buf = param_read_buf;

    ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
                                  desc_id, desc_index, selector, desc_buf,
                                  &desc_len);

    if (ret ||
            (desc_len > ufs_query_desc_max_size[desc_id]) ||
            (desc_buf[QUERY_DESC_DESC_TYPE_OFFSET] != desc_id)) {
        UFS_LOGE("[UFS] failed reading desc_id %d param_offset %d desc_len %d ret %d\n",
            desc_id, param_offset, desc_len, ret);

        if (!ret)
            ret = UFS_ERR_INVALID_DESCRIPTOR;

        goto out;
    }

out:

    return ret;
}

static int ufshcd_read_desc_length(struct ufs_hba *hba,
    enum desc_idn desc_id,
    int desc_index,
    u8 selector,
    u32 *desc_length)
{
    int ret;
    u8 header[QUERY_DESC_HDR_SIZE];
    u32 header_len = QUERY_DESC_HDR_SIZE;

    if (desc_id >= QUERY_DESC_IDN_MAX)
        return -1;

    ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
                    desc_id, desc_index, selector, header,
                    &header_len);

    if (ret) {
        UFS_LOGE("Failed to get descriptor header id %d\n", desc_id);
        return ret;
    } else if (desc_id != header[QUERY_DESC_DESC_TYPE_OFFSET]) {
        UFS_LOGE("descriptor header id %d and desc_id %d mismatch\n",
            header[QUERY_DESC_DESC_TYPE_OFFSET], desc_id);
        ret = -1;
    }

    *desc_length = header[QUERY_DESC_LENGTH_OFFSET];
    return ret;

}

static void ufshcd_init_desc_sizes(struct ufs_hba *hba)
{
    int err;
    static int init_done;

    if (init_done)
        return;

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_DEVICE, 0, 0,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE]);
    if (err)
        ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE] = QUERY_DESC_DEVICE_MAX_SIZE;
    UFS_LOGI("device descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_POWER, 0, 0,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_POWER]);
    if (err)
        ufs_query_desc_max_size[QUERY_DESC_IDN_POWER] = QUERY_DESC_POWER_MAX_SIZE;
    UFS_LOGI("power descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_POWER]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_INTERCONNECT, 0, 0,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_INTERCONNECT]);
    if (err)
        ufs_query_desc_max_size[QUERY_DESC_IDN_INTERCONNECT] = QUERY_DESC_INTERCONNECT_MAX_SIZE;
    UFS_LOGI("interconnect descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_INTERCONNECT]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_CONFIGURATION, 0, 0,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION]);
    if (err)
        ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION] = QUERY_DESC_CONFIGURAION_MAX_SIZE;
    UFS_LOGI("configuration descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_UNIT, 0, 0,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_UNIT]);
    if (err)
        ufs_query_desc_max_size[QUERY_DESC_IDN_UNIT] = QUERY_DESC_UNIT_MAX_SIZE;
    UFS_LOGI("unit descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_UNIT]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_GEOMETRY, 0, 0,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);
    if (err)
        ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY] = QUERY_DESC_GEOMETRY_MAX_SIZE;
    UFS_LOGI("geometry descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);

    init_done = 1;
}

static int ufshcd_init_vendor_desc_sizes(struct ufs_hba *hba)
{
    int err = 0;
    static int vendor_init_done;

    if (vendor_init_done)
        return err;

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_DEVICE, 0, 1,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE]);
    if (err)
        goto out;
    UFS_LOGI("vendor device descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_CONFIGURATION, 0, 1,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION]);
    if (err)
        goto out;
    UFS_LOGI("vendor configuration descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION]);

    err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_GEOMETRY, 0, 1,
        &ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);
    if (err)
        goto out;
    UFS_LOGI("vendor geometry descriptor length = 0x%x\n",
        ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);

out:
    vendor_init_done = 1;

    return err;
}

static void ufs_reset_device(struct ufs_hba *hba)
{
    UFS_LOGI("start\n");

    ufs_mtk_device_reset_ctrl(0);
    udelay(25);

    ufs_mtk_device_reset_ctrl(1);
    mdelay(10);

    UFS_LOGI("Done\n");
}

void ufs_reset_device_low(struct ufs_hba *hba)
{
    ufs_mtk_device_reset_ctrl(0);
}

int ufs_set_boot_lu(struct ufs_hba *hba, u32 b_boot_lun_en)
{
    int ret;

    ret = hba->query_attr(hba, UPIU_QUERY_OPCODE_WRITE_ATTR, ATTR_B_BOOT_LUN_EN, 0, 0,
        &b_boot_lun_en);

    if (ret != 0)
        UFS_LOGW("[UFS] err: error: %d\n", ret);
    else
        UFS_LOGI("[UFS] info: %d done\n", b_boot_lun_en);

    return ret;
}

int ufs_get_active_boot_part(u32 *active_boot_part)
{
    struct ufs_hba *hba = ufs_get_host(UFS_DEFAULT_HOST_ID);
    u32 b_boot_lun_en;
    int ret;

    if (!active_boot_part)
        return -1;

    ret = hba->query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR, ATTR_B_BOOT_LUN_EN, 0, 0,
                          &b_boot_lun_en);
    if (ret != UFS_ERR_NONE) {
        UFS_LOGE("[UFS] err: read ATTR_B_BOOT_LUN_EN error: %d\n", ret);
        return ret;
    }

    UFS_LOGI("%u part is active\n", b_boot_lun_en);
    if (b_boot_lun_en == ATTR_B_BOOT_LUN_EN_BOOT_LU_A)
        *active_boot_part = UFS_LU_BOOT1;
    else if (b_boot_lun_en == ATTR_B_BOOT_LUN_EN_BOOT_LU_B)
        *active_boot_part = UFS_LU_BOOT2;
    else {
        UFS_LOGE("[UFS] err: invalid ATTR_B_BOOT_LUN_EN %u\n", b_boot_lun_en);
        return -1;
    }


    return ret;
}

u32 ufs_get_lu_cfg_size(struct ufs_hba *hba, struct ufs_geometry_info *info, u32 lun_id)
{
    u32 d_num_alloc_units;

    UFS_LOGI("[UFS] info: lun %d\n", lun_id);

    UFS_LOGI("[UFS] d_segment_size=0x%x\n", info->d_segment_size);
    UFS_LOGI("[UFS] b_allocation_units_size=0x%x\n", info->b_allocation_units_size);
    UFS_LOGI("[UFS] q_total_raw_device_capacity=0x%lx\n",
        info->q_total_raw_device_capacity);

    if (lun_id < 2) {   /* boot lu */
        /*
         * Boot size is 4MB which have enhance feature, real size
         * should be 4MB * (w_adj_factor_enahnced_1 / 256)
         */
        UFS_LOGI("[UFS] w_adj_factor_enahnced_1=%d\n", info->w_adj_factor_enahnced_1);

        d_num_alloc_units = ((UFS_BOOT_LU_SIZE_BYTE >> 9) * (info->w_adj_factor_enahnced_1 >> 8)) /
                            info->d_segment_size / info->b_allocation_units_size;
    } else {            /* user lu */

        d_num_alloc_units = (u32)(((info->q_total_raw_device_capacity) -
            (u64)(2 * (UFS_BOOT_LU_SIZE_BYTE >> 9) * (info->w_adj_factor_enahnced_1 >> 8))) /
                info->d_segment_size / info->b_allocation_units_size);
    }

    UFS_LOGI("[UFS] d_num_alloc_units=0x%x\n", d_num_alloc_units);

    return d_num_alloc_units;
}

static void ufs_util_dump_config_table_hex(u8 *table, u32 bytes,
    u32 ud0_base_offset, u32 ud_config_len)
{
    u32 i, ptr;

    ptr = 0;

    for (i = 0; i < ud0_base_offset; i++) {
        printf("%x ", table[ptr]);
        ptr++;

        if (ptr >= bytes)
            break;
    }
    printf("\n");

    while (ptr < bytes) {

        for (i = 0; i < ud_config_len; i++) {
            printf("%x ", table[ptr]);
            ptr++;

            if (ptr >= bytes)
                break;
        }

        printf("\n");

        if (ptr >= bytes)
            return;
    }
}

static void ufs_util_dump_table_hex(u8 *table, u32 bytes)
{
    u32 i, ptr;

    ptr = 0;

    while (ptr < bytes) {

        for (i = 0; i < 16; i++) {
            printf("%x ", table[ptr]);
            ptr++;

            if (ptr >= bytes)
                break;
        }

        printf("\n");

        if (ptr >= bytes)
            return;
    }
}

int ufs_check_lu_cfg(struct ufs_hba *hba)
{
    int ret;
    u32 i;
    u8 *p;
    u32 num_active_lu = 0;
    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
    u8 *ufs_temp_buf = memalign(ufs->cache_line_size, UFS_TEMP_BUF_SIZE);
    /* malloc fail */
    UFS_ASSERT(ufs_temp_buf);

    /* HPB support, read configuration descriptor with HPB, selector = 1, length = 0xD0 */
    ret = hba->read_descriptor(hba, QUERY_DESC_IDN_CONFIGURATION, 0, hba->quirk_sel,
            (u8 *)ufs_temp_buf, ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION]);

    if (ret != 0) {
        UFS_LOGE("[UFS] err: read config descr error: %d\n", ret);
        free(ufs_temp_buf);
        return ret;
    }

    UFS_LOGI("[UFS] original Configuration Desc:\n");

    ufs_util_dump_config_table_hex(ufs_temp_buf,
        ufs_query_desc_max_size[QUERY_DESC_IDN_CONFIGURATION],
        hba->dev_info.ud0_base_offset, hba->dev_info.ud_config_len);

    p = (u8 *)ufs_temp_buf;

    /* check: number of active LU */
    for (i = 0; i < 8; i++) { /* traverse 8 Unit Descriptor configurable parameters */
        if (i == 0)
            p += hba->dev_info.ud0_base_offset;
        else
            p += hba->dev_info.ud_config_len;

        /* 01h: Logical Unit enabled, 02h: HPB Logical Unit enabled */
        if ((p[CONF_DESC_UNIT_B_LU_ENABLE] == 1) ||
                ((hba->dev_info.hpb_support) && (p[CONF_DESC_UNIT_B_LU_ENABLE] == 2)))
            num_active_lu++;

        if (i == 2) {
            /* check whether HPB is enabled for LU2 if available */
            if ((hba->dev_info.hpb_support) && (p[CONF_DESC_UNIT_B_LU_ENABLE] != 0x02))
                UFS_LOGE("[UFS] HPB is supported but not enabled on LU%d\n", i);

        }
    }

    hba->dev_info.num_active_lu = num_active_lu;

    free(ufs_temp_buf);
    return UFS_ERR_NONE;
}

int ufs_poweroff_notify(struct ufs_hba *hba)
{
    int ret = 0;

    ret = hba->set_dev_pwr(hba, UFS_POWERDOWN_PWR_MODE);

    return ret;
}

int ufs_get_device_info(struct ufs_hba *hba)
{
    struct ufs_scsi_cmd cmd;
    struct ufs_device_info *card_data;
    struct ufshcd_lrb *lrbp;
    int err;
    int tag;
    u8 *p = NULL, sel = 0;
    int str_len;
    u32 tmp_len = 0;
    int  i;
    bool vpd80 = false;
    u8 logical_blk_size;
    u32 logical_blk_size_bytes;
    u64 logical_blk_cnt;
    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
    zx_status_t status;
    u8* ufs_temp_buf;
    ufs_iobuf_t iob = {
        .size = UFS_TEMP_BUF_SIZE,
        .align = ufs->cache_line_size,
        .flags = IO_BUFFER_RW | IO_BUFFER_CONTIG,
    };

    status = ufs_alloc_iobuf(ufs, &iob);
    UFS_ASSERT(status == ZX_OK);
    ufs_temp_buf = iob.virt;

    card_data = &hba->dev_info;

    /* Have init and not reconfing device */
    if ((card_data->wmanufacturerid != 0) && (card_data->bootable == 1)) {
        ufs_free_iobuf(&iob);
        return 0;
    }

    memset(card_data, 0, sizeof(struct ufs_device_info));
    err = ufshcd_device_geo_params_init(hba);
    if (err) {
        UFS_LOGE("Failed getting geo info. err = %d\n", err);
        ufs_free_iobuf(&iob);
        return -1;
    }

    err = hba->read_descriptor(hba, QUERY_DESC_IDN_DEVICE, 0, 0, (u8 *)ufs_temp_buf,
        ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE]);

    if (err) {
        UFS_LOGE("[UFS] err: failed reading Device Desc. err = %d\n", err);
        ufs_free_iobuf(&iob);
        return -1;
    }

    p = (u8 *)ufs_temp_buf;

    card_data->ufs_ver = p[DEVICE_DESC_PARAM_SPEC_VER] << 8 |
                     p[DEVICE_DESC_PARAM_SPEC_VER + 1];
    UFS_LOGI("[UFS] UFS version %x.%x\n", p[DEVICE_DESC_PARAM_SPEC_VER],
        p[DEVICE_DESC_PARAM_SPEC_VER + 1]);

    UFS_LOGI("[UFS] Device Desc:\n");
    ufs_util_dump_table_hex(p, ufs_query_desc_max_size[QUERY_DESC_IDN_DEVICE]);

    if (p[DEVICE_DESC_PARAM_BOOT_ENBL] == 0)
        card_data->bootable = 0;
    else
        card_data->bootable = 1;

    /*
     * getting vendor (manufacturerID) and Bank Index in big endian format
     */
    card_data->wmanufacturerid = p[DEVICE_DESC_PARAM_MANF_ID] << 8 |
                                 p[DEVICE_DESC_PARAM_MANF_ID + 1];

    ufshcd_wb_probe(hba, ufs_temp_buf);

read_feature:
    UFS_LOGI("Try selector: 0x%x\n", sel);
        /* Check supported features */
    UFS_LOGI("Dev. Descr. F. Support: 0x%x\n", p[DEVICE_DESC_PARAM_FEAT_SUP]);
    UFS_LOGI("Dev. Descr. Ext. F. Support: 0x%x\n", p[DEVICE_DESC_PARAM_EXT_FEAT_SUP]);

    /* Check support HPB feature or not */
    if (p[DEVICE_DESC_PARAM_FEAT_SUP] & 0x80)
        card_data->hpb_support = 1;
    else
        card_data->hpb_support = 0;

    if (card_data->hpb_support) {
        card_data->hpb_ver = (p[DEVICE_DESC_PARAM_HPB_VER] << 8) |
                             p[DEVICE_DESC_PARAM_HPB_VER + 1];
        card_data->hpb_ctrl_mode = p[DEVICE_DESC_PARAM_HPB_CTRL_MODE];

        UFS_LOGW("HPB Version = %x.%x.%x, Control mode = %d\n",
                     (card_data->hpb_ver >> 8) & 0xFF,
            (card_data->hpb_ver >> 4) & 0xF,
            card_data->hpb_ver & 0xF, card_data->hpb_ctrl_mode);
    }

    /* Check support TW feature or not */
    if (p[DEVICE_DESC_PARAM_EXT_FEAT_SUP + 2] & 0x1)
        card_data->tw_support = 1;
    else
        card_data->tw_support = 0;

    if (card_data->tw_support) {
        card_data->tw_red = p[DEVICE_DESC_PARAM_TW_BUF_USER_REDUCED];
        card_data->tw_type = p[DEVICE_DESC_PARAM_TW_BUF_TYPE];
        UFS_LOGI("TW Reduction = %d, TW Type = %d\n",
            card_data->tw_red, card_data->tw_type);

        /* Only Samsung special ufs version have this field */
        if ((card_data->wmanufacturerid == UFS_VENDOR_SAMSUNG) && (sel == 0x01)) {
            if ((card_data->ufs_ver == 0x210) || (card_data->ufs_ver == 0x300))
                card_data->tw_ver = (p[0x55] << 8) | p[0x56];
            else
                card_data->tw_ver = (p[0x4D] << 8) | p[0x4E];

            UFS_LOGI("TW Version = %x.%x.%x\n",
                (card_data->tw_ver >> 8) & 0xFF,
                (card_data->tw_ver >> 4) & 0xF,
                card_data->tw_ver & 0xF);
        } else {
            card_data->wb_buf_au = (p[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU] << 24) |
                           (p[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU + 1] << 16) |
                           (p[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU + 2] << 5) |
                           (p[DEVICE_DESC_PARAM_NUM_SHARED_WB_BUF_AU + 3]);
            UFS_LOGI("Shared WB Buffer AU = 0x%x\n", card_data->wb_buf_au);
        }
    }

    /* Normal device:(0x10, 0x10) / HPB device:(0x10, 0x18) */
    card_data->ud0_base_offset = p[DEVICE_DESC_PARAM_UD_OFFSET];
    card_data->ud_config_len = p[DEVICE_DESC_PARAM_UD_LEN];
    UFS_LOGI("ud0_base_offset = %d, ud_config_len = %d\n",
        card_data->ud0_base_offset, card_data->ud_config_len);

    if (card_data->wmanufacturerid == UFS_VENDOR_SAMSUNG && (sel == 0x00)) {
        // TODO: remove after EOL of Selen and Santos

        err = ufshcd_read_desc_length(hba, QUERY_DESC_IDN_DEVICE, 0, 1, &tmp_len);
        err += hba->read_descriptor(hba, QUERY_DESC_IDN_DEVICE, 0, 1, (u8 *)ufs_temp_buf, tmp_len);

        if (err) {
            UFS_LOGE("[UFS] err: failed reading HPB Device Desc. err = %d\n", err);
            ufs_free_iobuf(&iob);
            return -1;
        }

        UFS_LOGI("[UFS] legacy Device Desc:\n");
        ufs_util_dump_table_hex(p, tmp_len);

        /*
         * Assume device after Solvit doesn't show HPB capability in DEVICE_DESC_PARAM_FEAT_SUP
         * with SEL=0x00
         */
        if (p[DEVICE_DESC_PARAM_FEAT_SUP] & 0x80) {
            sel++;
            err = ufshcd_init_vendor_desc_sizes(hba);
            if (err) {
                ufs_free_iobuf(&iob);
                return -1;
            }
            goto read_feature;
        }
    }

    // set quirk selector for later use
    hba->quirk_sel = sel;

    /* getting active LU number */
    /* card_data->num_active_lu = p[DEVICE_DESC_PARAM_NUM_LU]; */

    if (!ufshcd_get_free_tag(hba, &tag)) {
        ufs_free_iobuf(&iob);
        UFS_LOGW("[UFS] No free tag available\n");
        return -1;
    }

    lrbp = &hba->lrb[tag];
    lrbp->sync_cmd = true;
    memset(ufs_temp_buf, 0, QUERY_DESC_HEALTH_MAX_SIZE);
    ufs_dma_map(iob.virt, UFS_TEMP_BUF_SIZE, DMA_FROM_DEVICE);
    ufs_scsi_cmd_inquiry(&cmd, tag, 0, iob.phys, 0, 0);
    err = ufshcd_queuecommand(hba, &cmd);
    if (err) {
        UFS_LOGW("[UFS] err: ufshcd_queuecommand err\n");
        ufs_free_iobuf(&iob);
        ufshcd_put_tag(hba, tag);
        return -1;
    }

    p = (u8 *)ufs_temp_buf;

    /*
     * get product ID and product revision level (fw ver)
     * by INQUIRY command
     */

    /* product ID */
    str_len = ufs_util_sanitize_inquiry_string(&p[16], 16);
    if (str_len) {
        UFS_LOGW("[UFS] get product id string len 0x%x\n", str_len);
        memcpy(card_data->product_id, &p[16], str_len);
    }

    /* product revision level */
    str_len = ufs_util_sanitize_inquiry_string(&p[32], 4);
    if (str_len) {
        UFS_LOGW("[UFS] get revision level string len 0x%x\n", str_len);
        memcpy(card_data->product_revision_level, &p[32], str_len);
    }

    /* Serial number in VPD80 */
    memset(ufs_temp_buf, 0, QUERY_DESC_HEALTH_MAX_SIZE);
    ufs_dma_map(iob.virt, UFS_TEMP_BUF_SIZE, DMA_FROM_DEVICE);
    ufs_scsi_cmd_inquiry(&cmd, tag, 0, iob.phys, 1, 0);
    err = ufshcd_queuecommand(hba, &cmd);
    if (err) {
        UFS_LOGE("[UFS] err: ufshcd_queuecommand err\n");
        ufs_free_iobuf(&iob);
        ufshcd_put_tag(hba, tag);
        return -1;
    }

    for (i = 4; i < ufs_temp_buf[3]; i++) {
        if (ufs_temp_buf[i] == 0x80) {
            vpd80 = true;
            break;
        }
    }

    if (vpd80) {
        memset(ufs_temp_buf, 0, QUERY_DESC_HEALTH_MAX_SIZE);
        ufs_dma_map(iob.virt, UFS_TEMP_BUF_SIZE, DMA_FROM_DEVICE);
        ufs_scsi_cmd_inquiry(&cmd, tag, 0, iob.phys, 1, 0x80);
        err = ufshcd_queuecommand(hba, &cmd);
        if (err) {
            UFS_LOGE("[UFS] err: ufshcd_queuecommand err\n");
            ufs_free_iobuf(&iob);
            ufshcd_put_tag(hba, tag);
            return -1;
        }

        str_len = ufs_util_sanitize_inquiry_string(&p[4], ufs_temp_buf[3]);
        card_data->serial_number_len = str_len;
        if (str_len) {
            UFS_LOGE("[UFS] get unit serial number string len 0x%x\n", str_len);
            memcpy(card_data->serial_number, &p[4], str_len);
        }
    }

    /* Get rpmb lu size */
    memset(ufs_temp_buf, 0, ufs_query_desc_max_size[QUERY_DESC_IDN_UNIT]);
    err = hba->read_descriptor(hba, QUERY_DESC_IDN_UNIT, UFS_UPIU_RPMB_WLUN, 0,
        (u8 *)ufs_temp_buf, ufs_query_desc_max_size[QUERY_DESC_IDN_UNIT]);
    if (err) {
        UFS_LOGE("[UFS] err: failed reading RPMB UNIT Desc. err = %d\n", err);
        ufs_free_iobuf(&iob);
        ufshcd_put_tag(hba, tag);
        return -1;
    }

    logical_blk_size = ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_SIZE];

    for (i = 0, logical_blk_size_bytes = 1; i < logical_blk_size; i++)
        logical_blk_size_bytes = logical_blk_size_bytes << 1;

    UFS_LOGE("[UFS] rpmb: logical_blk_size: %d bytes\n",
        (u32) logical_blk_size_bytes);

    logical_blk_cnt = ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT] << 56) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 1] << 48) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 2] << 40) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 3] << 32) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 4] << 24) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 5] << 16) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 6] << 8) |
                  ((u64)ufs_temp_buf[UNIT_DESC_PARAM_LOGICAL_BLK_COUNT + 7]);

    UFS_LOGE("[UFS] rpmb: logical_blk_cnt: 0x%lx\n", logical_blk_cnt);
    card_data->rpmb_lu_size = logical_blk_size_bytes * logical_blk_cnt;

    /* Get rpmb rw size */
    memset(ufs_temp_buf, 0, ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);
    err = hba->read_descriptor(hba, QUERY_DESC_IDN_GEOMETRY, 0, 0,
        (u8 *)ufs_temp_buf, ufs_query_desc_max_size[QUERY_DESC_IDN_GEOMETRY]);
    if (err) {
        UFS_LOGE("[UFS] err: failed reading GEOMETRY Desc. err = %d\n", err);
        ufs_free_iobuf(&iob);
        ufshcd_put_tag(hba, tag);
        return -1;
    }
    card_data->rpmb_rw_size  = (u32)ufs_temp_buf[GEOMETRY_DESC_PARAM_RPMB_READ_WRITE_SIZE];
    UFS_LOGE("[UFS] rpmb: read write size: %d\n", card_data->rpmb_rw_size);


    ufshcd_put_tag(hba, tag);

    ufs_free_iobuf(&iob);

    UFS_LOGI("[UFS] quirk_sel: 0x%x\n", hba->quirk_sel);
    UFS_LOGI("[UFS] vendor id : 0x%x\n", hba->dev_info.wmanufacturerid);
    UFS_LOGI("[UFS] ufs id : %s\n", hba->dev_info.product_id);
    UFS_LOGI("[UFS] ufs fwver: %s\n", hba->dev_info.product_revision_level);
    UFS_LOGI("[UFS] ufs serial number: %s\n", hba->dev_info.serial_number);
    UFS_LOGI("[UFS] dev_quirk = 0x%x\n", hba->dev_quirks);

    return 0;
}

int ufs_get_lu_size(struct ufs_hba *hba, u32 lun, u32 *blk_size_in_byte, u32 *blk_cnt)
{
    struct ufs_scsi_cmd cmd;
    struct ufshcd_lrb *lrbp;
    int tag;
    int ret;
    u8 *p;
    u32 blk_cnt_int, blk_size_int;
    mtk_ufs_t* ufs = containerof(hba, mtk_ufs_t, hba);
    zx_status_t status;
    u8* ufs_temp_buf;
    ufs_iobuf_t iob = {
        .size = UFS_TEMP_BUF_SIZE,
        .align = ufs->cache_line_size,
        .flags = IO_BUFFER_RW | IO_BUFFER_CONTIG,
    };

    status = ufs_alloc_iobuf(ufs, &iob);
    UFS_ASSERT(status == ZX_OK);
    ufs_temp_buf = iob.virt;

    if (!ufshcd_get_free_tag(hba, &tag)) {
        ufs_free_iobuf(&iob);
        UFS_LOGW("[UFS] No free tag available\n");
        return -1;
    }

    // invalidate cache before dma
    ufs_dma_map(ufs_temp_buf, UFS_TEMP_BUF_SIZE, DMA_FROM_DEVICE);
    ufs_scsi_cmd_read_capacity(&cmd, (u32)tag, lun, iob.phys);
    lrbp = &hba->lrb[tag];
    lrbp->sync_cmd = true;
    ret = ufshcd_queuecommand(hba, &cmd);

    if (ret != UFS_ERR_NONE) {
        UFS_LOGE("[UFS] err: ufshcd_queuecommand err %d\n", ret);
        ufs_free_iobuf(&iob);
        ufshcd_put_tag(hba, tag);
        return ret;
    }

    ufshcd_put_tag(hba, tag);

    p = (u8 *)ufs_temp_buf;

    blk_cnt_int = (((u32)(p[0]) << 24) | ((u32)(p[1]) << 16) | ((u32)(p[2]) << 8) |
        (u32)(p[3])) + 1;

    if (blk_cnt)
        *blk_cnt = blk_cnt_int;

    blk_size_int = ((u32)(p[4]) << 24) | ((u32)(p[5]) << 16) | ((u32)(p[6]) << 8) | (u32)(p[7]);

    if (blk_size_in_byte)
        *blk_size_in_byte = blk_size_int;

    UFS_LOGE("[UFS] get lu size: lun=%d, blk_cnt=%d, blk_size_in_byte=%d\n",
                 lun, blk_cnt_int, blk_size_int);

    ufs_free_iobuf(&iob);
    return ret;
}

void ufs_dbg_show_reg(struct ufs_hba *hba, u32 addr)
{
    u32 data;

    data = ufshcd_readl(hba, addr);

    UFS_LOGW("\t: 0x%x, 0x%x\n", addr, data);
}

int ufs_dma_dme_get(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val)
{
    return ufshcd_dme_get_attr(hba, attr_sel, mib_val, DME_LOCAL);
}

int ufs_dma_dme_peer_get(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val)
{
    return ufshcd_dme_get_attr(hba, attr_sel, mib_val, DME_PEER);
}

int ufs_dma_dme_set(struct ufs_hba *hba, u32 attr_sel, u32 mib_val)
{
    return ufshcd_dme_set_attr(hba, attr_sel, ATTR_SET_NOR, mib_val, DME_LOCAL);
}

int ufs_dma_dme_peer_set(struct ufs_hba *hba, u32 attr_sel, u32 mib_val)
{
    return ufshcd_dme_set_attr(hba, attr_sel, ATTR_SET_NOR, mib_val, DME_PEER);
}

int ufs_dma_nopin_nopout(struct ufs_hba *hba)
{
    int err = 0;
    int retries;

    for (retries = NOP_OUT_RETRIES; retries > 0; retries--) {
        err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_NOP,
                                  NOP_OUT_TIMEOUT);

        if (!err)
            break;
    }

    if (err)
        UFS_LOGE("[UFS] err: NOP OUT failed\n");

    return err;
}

int ufs_dma_query_flag(struct ufs_hba *hba, enum query_opcode opcode,
                           enum flag_idn idn, int index, bool *flag_res)
{
    struct ufs_query_req *request = NULL;
    struct ufs_query_res *response = NULL;
    int err, selector = 0;

    UFS_ASSERT(hba);

    ufshcd_init_query(hba, &request, &response, opcode, idn, index,
                      selector);

    switch (opcode) {
    case UPIU_QUERY_OPCODE_SET_FLAG:
    case UPIU_QUERY_OPCODE_CLEAR_FLAG:
    case UPIU_QUERY_OPCODE_TOGGLE_FLAG:
        request->query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST;
        break;
    case UPIU_QUERY_OPCODE_READ_FLAG:
        request->query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQUEST;
        if (!flag_res) {
            /* No dummy reads */
            UFS_LOGW("Invalid argument for read request\n");
            err = -1;
            goto out_unlock;
        }
        break;
    default:
        UFS_LOGW("Expected query flag opcode but got = %d\n", opcode);
        err = -1;
        goto out_unlock;
    }

    err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_QUERY, QUERY_REQ_TIMEOUT);

    if (err) {
        UFS_LOGW("Sending flag query for idn %d failed, err = %d\n", idn, err);
        goto out_unlock;
    }

    if (flag_res)
        *flag_res = (__builtin_bswap32(response->upiu_res.value) &
                     MASK_QUERY_UPIU_FLAG_LOC) & 0x1;

out_unlock:

    return err;
}

int ufs_dma_query_attr(struct ufs_hba *hba, enum query_opcode opcode,
                           enum attr_idn idn, u8 index, u8 selector, u32 *attr_val)
{
    struct ufs_query_req *request = NULL;
    struct ufs_query_res *response = NULL;
    int err;

    if (!attr_val) {
        UFS_LOGW("attribute value required for opcode 0x%x\n", opcode);
        err = -1;
        goto out;
    }

    ufshcd_init_query(hba, &request, &response, opcode, idn, index,    selector);

    switch (opcode) {
    case UPIU_QUERY_OPCODE_WRITE_ATTR:
        request->query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST;
        request->upiu_req.value = __builtin_bswap32(*attr_val);
        break;
    case UPIU_QUERY_OPCODE_READ_ATTR:
        request->query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQUEST;
        break;
    default:
        UFS_LOGW("Expected query attr opcode but got = 0x%.2x\n", opcode);
        err = -1;
        goto out;
    }

    err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_QUERY, QUERY_REQ_TIMEOUT);

    if (err) {
        UFS_LOGW("opcode 0x%x for idn %d failed, err = %d\n", opcode, idn, err);
        goto out;
    }

    *attr_val = __builtin_bswap32(response->upiu_res.value);

out:

    return err;
}

int ufs_dma_read_desc(struct ufs_hba *hba, enum desc_idn desc_id, int desc_index,
    u8 selector, u8 *buf, u32 size)
{
    return ufshcd_read_desc_param(hba, desc_id, desc_index, selector, 0, buf, size);
}

int ufs_dma_write_desc(struct ufs_hba *hba, enum desc_idn idn, int index, u8 selector,
    u8 *src_buf, u32 buf_len)
{
    u32 buf_len_local = buf_len;

    return ufshcd_query_descriptor(hba, UPIU_QUERY_OPCODE_WRITE_DESC, idn, index, selector,
        src_buf, &buf_len_local);
}

static void ufshcd_set_link_active(struct ufs_hba *hba)
{
    /* In Garnet, we may not need to track link state explicitly */
    /* This is a stub to match kernel API */
}

static u32 ufs_mtk_get_link_state(struct ufs_hba *hba)
{
    ufshcd_writel(hba, 0x820820, REG_UFS_DEBUG_SEL);
    ufshcd_writel(hba, 0x0, REG_UFS_DEBUG_SEL_B0);
    ufshcd_writel(hba, 0x55555555, REG_UFS_DEBUG_SEL_B1);
    ufshcd_writel(hba, 0xaaaaaaaa, REG_UFS_DEBUG_SEL_B2);
    ufshcd_writel(hba, 0xffffffff, REG_UFS_DEBUG_SEL_B3);

    return ufshcd_readl(hba, REG_UFS_PROBE) >> 28;
}

static int ufs_mtk_wait_idle_state(struct ufs_hba *hba, unsigned long retry_ms)
{
    unsigned int timeout_us = retry_ms * 1000;
    unsigned int delay_us = 50;
    u32 val, sm;
    bool wait_idle = false;

    /* wait a specific time after check base */
    udelay(10);

    while (timeout_us > 0) {
        /* ufs_mtk_dbg_sel is not implemented in Garnet */
        val = ufshcd_readl(hba, REG_UFS_PROBE);
        sm = val & 0x1f;

        /*
         * if state is in H8 enter and H8 enter confirm
         * wait until return to idle state.
         */
        if ((sm >= VS_HIB_ENTER) && (sm <= VS_HIB_EXIT)) {
            wait_idle = true;
            udelay(delay_us);
            timeout_us -= delay_us;
            continue;
        } else if (!wait_idle) {
            break;
        }

        if (wait_idle && (sm == VS_HCE_BASE)) {
            break;
        }

        udelay(delay_us);
        timeout_us -= delay_us;
    }

    if (wait_idle && (val & 0x1f) != VS_HCE_BASE) {
        UFS_LOGW("wait idle timeout: 0x%x\n", val);
        return -1;
    }
    return 0;
}

static int ufs_mtk_wait_link_state(struct ufs_hba *hba, u32 state, unsigned long max_wait_ms)
{
    unsigned int timeout_us = max_wait_ms * 1000;
    unsigned int delay_us = 100;
    u32 val;

    while (timeout_us > 0) {
        val = ufs_mtk_get_link_state(hba);
        if (val == state) {
            break;
        }

        udelay(delay_us);
        timeout_us -= delay_us;
    }

    if (timeout_us <= 0) {
        UFS_LOGW("wait link state 0x%x timeout: 0x%x\n", state, val);
        return -1;
    }
    return 0;
}

static int ufs_mtk_unipro_set_lpm(struct ufs_hba *hba, bool lpm)
{
	int ret;

    ret = hba->dme_set(hba, UIC_ARG_MIB_SEL(VENDOR_UNIPROPOWERDOWNCONTROL, 0), lpm ? 1 : 0);
    if (!ret || !lpm)
        hba->unipro_lpm = lpm;

    if (ret) {
        UFS_LOGE("failed to set unipro power down control lpm:%d ret:%d\n", lpm, ret);
    }
    UFS_LOGI("Done set unipro power down control lpm:%d ret:%d\n", lpm, ret);

	return ret;
}

static void ufs_mtk_auto_hibern8_disable(struct ufs_hba *hba)
{
    int ret;

    ufshcd_writel(hba, 0, REG_AHIT);

    udelay(100);
    UFS_LOGW("skip check ahit status\n");
    return;

    ufs_mtk_wait_idle_state(hba, 5);

    ret = ufs_mtk_wait_link_state(hba, VS_LINK_UP, 100);
    if (ret)
        UFS_LOGW("exit h8 state fail, ret=%d\n", ret);
}

static inline void ufs_mtk_auto_hibern8_enable(struct ufs_hba *hba)
{
    ufshcd_writel(hba, UFS_MTK_DEFAULT_AHIT, REG_AHIT);
}

static int ufs_mtk_link_set_lpm(struct ufs_hba *hba)
{
	int err;

	UFS_LOGW("[UFS] link_set_lpm start\n");
	/* not wait unipro resetCnf */
	ufshcd_writel(hba,
		(ufshcd_readl(hba, REG_UFS_XOUFS_CTRL) & ~0x100),
		REG_UFS_XOUFS_CTRL);

	err = ufs_mtk_unipro_set_lpm(hba, true);
	if (err) {
		/* Resume UniPro state for following error recovery */
		ufs_mtk_unipro_set_lpm(hba, false);
		UFS_LOGW("unipro lpm fail, err=%d\n", err);
		return err;
	}
	UFS_LOGW("[UFS] link_set_lpm end\n");

	return 0;
}

static int ufs_mtk_link_set_hpm(struct ufs_hba *hba)
{
	int err;
	u32 val;
	UFS_LOGI("[UFS] link_set_hpm start\n");

	err = ufshcd_hba_enable(hba, true);
	if (err)
		return err;
	UFS_LOGI("[UFS] ufshcd_hba_enable done\n");

	err = ufs_mtk_unipro_set_lpm(hba, false);
	if (err) {
		/* Log error registers for debugging */
		val = ufshcd_readl(hba, REG_UFS_PROBE);
		ufshcd_update_evt_hist(hba, UFS_EVT_RESUME_ERR, (u32)val);
		val = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
		ufshcd_update_evt_hist(hba, UFS_EVT_RESUME_ERR, (u32)val);
		UFS_LOGE("failed to set hpm REG_UFS_PROBE:%x REG_INTERRUPT_STATUS:%x\n",
			val, ufshcd_readl(hba, REG_INTERRUPT_STATUS));
		return err;
	}
	UFS_LOGI("[UFS] ufs_mtk_unipro_set_lpm done\n");

	err = ufshcd_uic_hibern8_exit(hba);
	if (err) {
		UFS_LOGE("ufs_mtk_uic_hibern8_exit fail, err=%d\n", err);
		return err;
	}
	UFS_LOGI("[UFS] ufshcd_uic_hibern8_exit done\n");

	/* Check link state to make sure exit h8 success */
	ufs_mtk_wait_idle_state(hba, 5);
	err = ufs_mtk_wait_link_state(hba, VS_LINK_UP, 100);
	if (err) {
		UFS_LOGW("exit h8 state fail, err=%d\n", err);
		return err;
	}
	ufshcd_set_link_active(hba);
	UFS_LOGI("[UFS] ufs_mtk_wait_link_state done\n");

	err = ufshcd_make_hba_operational(hba);
	if (err)
		return err;
	UFS_LOGI("[UFS] link_set_hpm done\n");

	return 0;
}

static int ufs_mtk_mphy_power_on(struct ufs_hba *hba, bool on)
{
    int ret = 0;

    if (!(on ^ hba->mphy_powered_on))
        return 0;

    /*
     * Garnet does not have the Linux PHY/regulator framework here.
     * Keep the same high-level sequencing as kernel by toggling the
     * VA09 rail and saving/restoring MPHY state through SiP hooks.
     */
    if (on) {
        /* Enable VA09 rail */
        ufs_mtk_va09_pwr_ctrl(true);
        /* wait 200 us to stabilize VA09 */
        udelay(200);
        /* Restore MPHY state */
        ufs_mtk_mphy_ctrl(UFS_MPHY_RESTORE);
        ufs_mtk_mphy_set_active(hba);
    } else {
        /* Backup MPHY state */
        ufs_mtk_mphy_ctrl(UFS_MPHY_BACKUP);
        ufs_mtk_mphy_set_deep_hibern(hba);
        /* Disable VA09 rail */
        ufs_mtk_va09_pwr_ctrl(false);
    }
    UFS_LOGI("MPHY powered %s\n", on ? "on" : "off");
    hba->mphy_powered_on = on;
    return ret;
}

static int ufs_mtk_pwr_ctrl(struct ufs_hba *hba, bool on)
{
    int ret;

    if (on) {
        ret = ufs_mtk_mphy_power_on(hba, true);
        if (ret) {
            return ret;
        }

        return ufs_mtk_setup_ref_clk(hba, true);
    }

    ret = ufs_mtk_setup_ref_clk(hba, false);
    if (ret)
        return ret;

    return ufs_mtk_mphy_power_on(hba, false);
}

void ufshcd_irq_ctrl(struct ufs_hba *hba, bool on)
{
    if (on) {
        //clear the interrupt status register
        ufshcd_readl(hba, REG_INTERRUPT_STATUS);
        zx_interrupt_unmask(hba->irq_handle, hba->irq);
    } else
        zx_interrupt_mask(hba->irq_handle, hba->irq);
}

static void ufshcd_resume_dump_state(struct ufs_hba *hba, const char *stage)
{
    u32 intr = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
    u32 intr_en = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
    u32 tr_db = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
    u32 tm_db = ufshcd_readl(hba, REG_UTP_TASK_REQ_DOOR_BELL);

    (void)intr;
    (void)intr_en;
    (void)tr_db;
    (void)tm_db;

    UFS_LOGD("resume[%s]: intr=0x%x en=0x%x tr_db=0x%x tm_db=0x%x outstanding=0x%lx lrb=0x%lx irq_status=0x%x\n",
             stage, intr, intr_en, tr_db, tm_db,
             hba->outstanding_reqs, hba->lrb_in_use, hba->irq_status);
}

static void ufshcd_suspend_dump_state(struct ufs_hba *hba, const char *stage)
{
    u32 intr = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
    u32 intr_en = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
    u32 tr_db = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
    u32 tm_db = ufshcd_readl(hba, REG_UTP_TASK_REQ_DOOR_BELL);

    (void)intr;
    (void)intr_en;
    (void)tr_db;
    (void)tm_db;

    UFS_LOGD("suspend[%s]: intr=0x%x en=0x%x tr_db=0x%x tm_db=0x%x outstanding=0x%lx lrb=0x%lx irq_status=0x%x\n",
             stage, intr, intr_en, tr_db, tm_db,
             hba->outstanding_reqs, hba->lrb_in_use, hba->irq_status);
}

static void ufshcd_suspend_reconcile_stale_reqs(struct ufs_hba *hba, const char *stage)
{
    unsigned long stale_reqs;
    u32 tr_db;
    int i;

    tr_db = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);

    UFS_LOGD("suspend[%s]: tr_db=0x%x\n", stage, tr_db);
    mutex_lock(&hba->outstanding_lock);
    stale_reqs = hba->outstanding_reqs & ~((unsigned long)tr_db);
    hba->outstanding_reqs &= (unsigned long)tr_db;
    mutex_unlock(&hba->outstanding_lock);

    if (!stale_reqs)
        return;

    UFS_LOGW("suspend[%s]: stale outstanding=0x%lx tr_db=0x%x lrb=0x%lx\n",
             stage, stale_reqs, tr_db, hba->lrb_in_use);

    for (i = 0; i < hba->nutrs; i++) {
        struct ufshcd_lrb *lrbp;

        if (!(stale_reqs & (1UL << i)))
            continue;

        lrbp = &hba->lrb[i];
        UFS_LOGW("suspend[%s]: recover tag=%d sync=%d cmd_type=%u cmd=%p bop=%p\n",
                 stage, i, lrbp->sync_cmd, lrbp->command_type, lrbp->cmd, lrbp->bop);

        lrbp->cmd = NULL;
        lrbp->bop = NULL;
        ufshcd_put_tag(hba, i);
    }
}

/*
 * Resume preflight:
 * - clear stale event signal and latched IRQ status
 * - if doorbell is set while SW has no outstanding request, clear HW list
 */
static void ufshcd_resume_sanitize_io(struct ufs_hba *hba)
{
    u32 intr_status;
    u32 tr_db;
    u32 tm_db;

    zx_object_signal(hba->irq_event, UFS_IRQ_RECEIVED, 0);
    hba->irq_status = 0;

    intr_status = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
    if (intr_status) {
        ufshcd_writel(hba, intr_status & (~UFSHCD_UIC_MASK), REG_INTERRUPT_STATUS);
        UFS_LOGD("resume: clear stale intr=0x%x\n", intr_status);
    }

    tr_db = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
    if (tr_db && hba->outstanding_reqs == 0) {
        ufshcd_writel(hba, tr_db, REG_UTP_TRANSFER_REQ_LIST_CLEAR);
        UFS_LOGW("resume: clear stale tr doorbell=0x%x\n", tr_db);
    }

    tm_db = ufshcd_readl(hba, REG_UTP_TASK_REQ_DOOR_BELL);
    if (tm_db) {
        ufshcd_writel(hba, tm_db, REG_UTP_TASK_REQ_LIST_CLEAR);
        UFS_LOGW("resume: clear stale tm doorbell=0x%x\n", tm_db);
    }
}

static int __attribute__((unused)) ufshcd_setup_vreg(struct ufs_hba *hba, bool on)
{
    //vcc control
    return 0;
}

static void __attribute__((unused)) ufs_mtk_vccqx_set_lpm(struct ufs_hba *hba, bool lpm)
{
    //vccq lpm
}

static void ufs_mtk_vsx_set_lpm(struct ufs_hba *hba, bool lpm)
{
    ufs_mtk_device_pwr_ctrl(!lpm, (unsigned long)hba->dev_info.ufs_ver);
}

static void ufs_mtk_dev_vreg_set_lpm(struct ufs_hba *hba, bool lpm)
{
    /* Hypervisor UFS does not expose Linux's regulator objects; use VSX power state. */
    ufs_mtk_vsx_set_lpm(hba, lpm);
}

int ufs_suspend(struct ufs_hba *hba)
{
    int ret = 0;
    int timeout = 5000;
    bool idle = false;
    u32 link_state, try_cnt = 0;
    u32 tr_db;
    unsigned long outstanding;
    unsigned long lrb_in_use;

    UFS_LOGI("Suspend start: link status: %d\n", ufs_mtk_get_link_state(hba));

    ufs_mtk_auto_hibern8_disable(hba);

    ufshcd_wb_need_flush(hba);

    ufshcd_suspend_dump_state(hba, "pre-quiesce");

    //wait for current request
    do {
        tr_db = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);

        mutex_lock(&hba->outstanding_lock);
        outstanding = hba->outstanding_reqs;
        mutex_unlock(&hba->outstanding_lock);

        mutex_lock(&hba->free_tag_lock);
        lrb_in_use = hba->lrb_in_use;
        mutex_unlock(&hba->free_tag_lock);

        if ((lrb_in_use == 0) && (outstanding == 0) && (tr_db == 0)) {
            idle = true;
            break;
        }

        if ((tr_db == 0) && (outstanding != 0)) {
            ufshcd_suspend_reconcile_stale_reqs(hba, "wait-idle");
        }

        mdelay(1);
    } while (timeout--);

    if (!idle) {
        ufshcd_suspend_dump_state(hba, "wait-timeout");
        UFS_LOGW("suspend idle timeout\n");
        return -1;
    }

    UFS_LOGD("Suspend step: set device sleep power mode\n");
    ret = hba->set_dev_pwr(hba, UFS_SLEEP_PWR_MODE);
    if (ret) {
        UFS_LOGW("failed to set sleep power mode ret:%d\n", ret);
    }
    ufshcd_suspend_reconcile_stale_reqs(hba, "after-set-dev-pwr");
    ufshcd_suspend_dump_state(hba, "after-set-dev-pwr");

    UFS_LOGD("Suspend step: set uic hibern8 enter\n");
    ret = ufshcd_uic_hibern8_enter(hba);
    if (ret) {
        UFS_LOGW("failed to enter hibern8 ret:%d\n", ret);
    } else {
        UFS_LOGD("Suspend step done: enter hibern8\n");
    }
    ufshcd_suspend_reconcile_stale_reqs(hba, "after-h8-enter");
    ufshcd_suspend_dump_state(hba, "after-h8-enter");

    UFS_LOGD("Suspend step: set mtk link lpm\n");
RETRY_LPM:
    link_state = ufs_mtk_get_link_state(hba);
    UFS_LOGD("Suspend check: link state before link_set_lpm=0x%x\n", link_state);
    if (link_state == VS_LINK_HIBERN8) {
        UFS_LOGD("Suspend step: set link lpm\n");
        ret = ufs_mtk_link_set_lpm(hba);
        if (ret) {
            UFS_LOGW("failed to set link lpm ret:%d\n", ret);
        } else {
            UFS_LOGD("Suspend step done: set link lpm\n");
        }
    } else {
        UFS_LOGW("link is not in hibern8 before lpm, state:0x%x\n",
            link_state);
        ret = ufshcd_uic_hibern8_enter(hba);
        if (ret) {
            UFS_LOGW("failed to enter hibern8 ret:%d\n", ret);
        } else {
            UFS_LOGD("Suspend step done: enter hibern8\n");
        }
        if (try_cnt++ < 3)
            goto RETRY_LPM;
    }

    UFS_LOGD("Suspend step: disable irq\n");
    ufshcd_suspend_dump_state(hba, "before-disable-irq");
    ufshcd_irq_ctrl(hba, false);

    UFS_LOGD("Suspend step: power down refclk/mphy\n");
    ret = ufs_mtk_pwr_ctrl(hba, false);
    if (ret) {
        UFS_LOGW("failed to power down clk/phy ret:%d\n", ret);
    } else {
        UFS_LOGD("Suspend step done: power down refclk/mphy\n");
    }

    UFS_LOGD("Suspend step: sram power off\n");
    ufs_mtk_sram_pwr_ctrl(false);

    UFS_LOGD("Suspend step: gate ufs clocks\n");
    ufshcd_setup_ufs_clocks(hba, false);

    UFS_LOGD("Suspend step: set device vreg lpm\n");
    ufs_mtk_dev_vreg_set_lpm(hba, true);

    UFS_LOGD("Suspend step: mtcmos off\n");
    ufs_mtk_mtcmos_ctrl(false);

    //UFS_LOGI("Suspend step: Disable ufs regulator\n");
    //ufs_mtk_vreg_ctrl(false);

    UFS_LOGI("Suspend done: ret=%d\n", ret);

    return ret;
}

int ufs_resume(struct ufs_hba *hba)
{
    int ret = 0;
    u32 link_state;

    UFS_LOGI("Resume start\n");

    //UFS_LOGI("Enable ufs regulator\n");
    //ufs_mtk_vreg_ctrl(true);

    UFS_LOGD("Resume step: mtcmos on\n");
    ufs_mtk_mtcmos_ctrl(true);

    UFS_LOGD("Resume step: set device vreg active\n");
    ufs_mtk_dev_vreg_set_lpm(hba, false);

    UFS_LOGD("Resume step: ungate ufs clocks\n");
    ufshcd_setup_ufs_clocks(hba, true);

    UFS_LOGD("Resume step: sram power on\n");
    ufs_mtk_sram_pwr_ctrl(true);

    UFS_LOGD("Resume step: power up mphy/refclk\n");
    ret = ufs_mtk_pwr_ctrl(hba, true);
    if (ret) {
        UFS_LOGW("failed to power up clk/phy ret:%d\n", ret);
    } else {
        UFS_LOGD("Resume step done: power up mphy/refclk\n");
    }

    ufshcd_resume_dump_state(hba, "pre-sanitize");
    ufshcd_resume_sanitize_io(hba);
    ufshcd_resume_dump_state(hba, "post-sanitize");

    UFS_LOGD("Resume step: enable irq\n");
    ufshcd_irq_ctrl(hba, true);

    link_state = ufs_mtk_get_link_state(hba);
    UFS_LOGD("Resume check: link state before link_set_hpm=0x%x\n", link_state);

    UFS_LOGD("Resume step: set link hpm\n");
    ret = ufs_mtk_link_set_hpm(hba);
    if (ret) {
        UFS_LOGW("failed to set link hpm ret:%d\n", ret);
    } else {
        UFS_LOGD("Resume step done: set link hpm\n");
    }
    UFS_LOGD("Resume: ret=%d, link_state=0x%x\n", ret, ufs_mtk_get_link_state(hba));

    ufshcd_resume_dump_state(hba, "before-set_dev_pwr");
    ret = hba->set_dev_pwr(hba, UFS_ACTIVE_PWR_MODE);
    if (ret) {
        UFS_LOGW("failed to set active power mode ret:%d\n", ret);
    }

    UFS_LOGD("Resume step: enable auto hibern8\n");
    ufs_mtk_auto_hibern8_enable(hba);

    UFS_LOGI("Resume done: ret=%d, link_state=0x%x\n", ret, ufs_mtk_get_link_state(hba));

    return 0;
}

static void ufshcd_complete_requests(struct ufs_hba *hba)
{
	ufshcd_transfer_req_compl(hba);
}

/**
 * ufshcd_saved_requests_requeue
 * @hba: per adapter instance
 *
 * This function completes all requests that were pending before reset with
 * an error status. It processes requests saved by ufshcd_save_pending_requests.
 *
 * Returns: number of requests failed
 */
static int ufshcd_saved_requests_requeue(struct ufs_hba *hba)
{
	struct ufshcd_lrb *lrbp;
	int tag;
	int requeue_count = 0;
	unsigned long saved_reqs;
	unsigned long saved_lrb;

	mtx_lock(&hba->free_tag_lock);
	saved_reqs = hba->outstanding_reqs;
	saved_lrb = hba->lrb_in_use;
	mtx_unlock(&hba->free_tag_lock);

	if (saved_reqs == 0 && saved_lrb == 0) {
		UFS_LOGE("No saved requests to fail\n");
		return 0;
	}

	UFS_LOGE("Requeue saved requests after reset\n");
	UFS_LOGE("saved_outstanding_reqs=0x%lx, saved_lrb_in_use=0x%lx\n", saved_reqs, saved_lrb);

	/* Process all saved requests */
	for (tag = 0; tag < hba->nutrs; tag++) {
		/* Check if this tag was in use before reset */
		if ((saved_lrb & (1UL << tag)) == 0)
			continue;

		lrbp = &hba->lrb[tag];

		UFS_LOGE("Processing saved request tag=%d, cmd_type=%d, lun=%d\n", tag, lrbp->command_type, lrbp->lun);

		/* Handle SCSI commands with block operations */
		if (lrbp->command_type == UTP_CMD_TYPE_SCSI && lrbp->bop) {
			UFS_LOGE("Requeue SCSI request tag %d, lun %d due to reset\n", tag, lrbp->lun);

			ufshcd_queuecommand(hba, lrbp->cmd);

			requeue_count++;
		}
	}

	UFS_LOGE("Requeue %d saved requests after reset\n", requeue_count);

	return requeue_count;
}

/**
 * ufshcd_host_reset_and_restore - Reset host controller and restore device
 * @hba: per adapter instance
 *
 * This function performs a software reset of the UFS host controller
 * (without resetting the device) and re-establishes the link and device state.
 * This is less invasive than a full device reset.
 *
 * Use Case:
 * - Called after device reset (ufshcd_device_reset) completes
 * - Used during error recovery when host state is corrupted
 * - Restores host-side state while device keeps its configuration
 *
 * Context: Called from error handler thread (ufshcd_reset_and_restore)
 * Returns: 0 on success, negative error code on failure
 */
static int ufshcd_host_reset_and_restore(struct ufs_hba *hba)
{
	int err = 0;

	UFS_LOGE("\n");

	err = ufshcd_hba_enable(hba, true);
	if (err) {
		UFS_LOGE("[UFS] err: Host controller enable failed\n");
	}

	if (!err) {
		completion_reset(&hba->tag_release);
		err = ufshcd_probe_hba(hba);
	}

	if (err)
		UFS_LOGE("Host init failed %d\n", err);

	ufshcd_update_evt_hist(hba, UFS_EVT_HOST_RESET, (u32)err);

	return err;
}

/**
 * ufshcd_reset_and_restore - Complete UFS reset and recovery procedure
 * @hba: per adapter instance
 *
 * This is the top-level error recovery function that performs a complete
 * reset of both the UFS device and host controller, then restores the
 * system to operational state. This is the most comprehensive reset
 * procedure in the UFS driver.
 *
 * Context: Must be called from error handler thread (ufshcd_err_handler)
 * Thread Safety: Serialized by eh_flags (UFSHCD_EH_IN_PROGRESS)
 * Returns: 0 on success, negative error code on failure
 */
static int ufshcd_reset_and_restore(struct ufs_hba *hba)
{
	u32 saved_err = 0;
	u32 saved_uic_err = 0;
	int err = 0;
	int retries = MAX_HOST_RESET_RETRIES;

	UFS_LOGE("Starting reset and restore procedure\n");

	do {
		/*
		 * This is a fresh start, cache and clear saved error first,
		 * in case new error generated during reset and restore.
		 */
		mtx_lock(&hba->free_tag_lock);
		saved_err |= hba->saved_err;
		saved_uic_err |= hba->saved_uic_err;
		hba->saved_err = 0;
		hba->saved_uic_err = 0;
		hba->force_reset = false;
		mtx_unlock(&hba->free_tag_lock);

		err = ufshcd_host_reset_and_restore(hba);

		if (err)
			continue;
	} while (err && --retries);

	if (err) {
		mtx_lock(&hba->free_tag_lock);
		hba->saved_err |= saved_err;
		hba->saved_uic_err |= saved_uic_err;
		mtx_unlock(&hba->free_tag_lock);
		UFS_LOGE("Reset failed after %d retries\n", MAX_HOST_RESET_RETRIES);
	} else {
		UFS_LOGE("Reset completed successfully\n");
	}

	ufshcd_saved_requests_requeue(hba);

	return err;
}
