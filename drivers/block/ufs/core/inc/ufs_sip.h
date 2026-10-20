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

/*
 * SiP commands
 */
#define MTK_SIP_UFS_CONTROL               0xc2000276
#define MTK_SIP_KERNEL_UFS_CONTROL        0xC2000276UL
#define MTK_SIP_KERNEL_CLK_CONTROL        0xC2000550UL
#define MTK_SIP_HYP_UFS_CONTROL           0xC2000833UL
#define UFS_MTK_SIP_VA09_PWR_CTRL         UFS_BIT(0)
#define UFS_MTK_SIP_DEVICE_RESET          UFS_BIT(1)
#define UFS_MTK_SIP_CRYPTO_CTRL           UFS_BIT(2)
#define UFS_MTK_SIP_REF_CLK_NOTIFICATION  UFS_BIT(3)
#define UFS_MTK_SIP_SRAM_PWR_CTRL         UFS_BIT(5)
#define UFS_MTK_SIP_GET_VCC_NUM           UFS_BIT(6)
#define UFS_MTK_SIP_DEVICE_PWR_CTRL       UFS_BIT(7)
#define UFS_MTK_SIP_MPHY_CTRL             UFS_BIT(8)
#define UFS_MTK_SIP_MTCMOS_CTRL           UFS_BIT(9)
#define UFS_MTK_SIP_RPMB_KEY              UFS_BIT(10)
#define UFS_MTK_SIP_PWR_CTRL              UFS_BIT(11)

/*
 * Multi-VCC by Numbering
 */
enum ufs_mtk_vcc_num {
	UFS_VCC_NONE = 0,
	UFS_VCC_1,
	UFS_VCC_2,
	UFS_VCC_MAX
};

enum ufs_mtk_mphy_op {
	UFS_MPHY_BACKUP = 0,
	UFS_MPHY_RESTORE
};

/*
 * SMC call wapper function
 */
struct ufs_mtk_smc_arg {
	unsigned long cmd;
	unsigned long v1;
	unsigned long v2;
	unsigned long v3;
	unsigned long v4;
	unsigned long v5;
	unsigned long v6;
	unsigned long v7;
};

static inline void ufs_mtk_smc(unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
    zx_status_t status;
    zx_smc_result_t result;
    zx_smc_parameters_t parameters;

    parameters.func_id = MTK_SIP_KERNEL_UFS_CONTROL;
    parameters.arg1 = arg1;
    parameters.arg2 = arg2;
    parameters.arg3 = arg3;
    parameters.arg4 = 0;
    parameters.arg5 = 0;
    parameters.arg6 = 0;
    parameters.client_id = 0;
    parameters.secure_os_id = 0;

    status = zx_smc_call(get_root_resource(), &parameters, &result);
    if (status != ZX_OK) {
        UFS_LOGE("Failed to send smcall %d\n", status);
    }
}

static inline void clk_mtk_smc(unsigned long arg1)
{
    zx_status_t status;
    zx_smc_result_t result;
    zx_smc_parameters_t parameters;

    parameters.func_id = MTK_SIP_KERNEL_CLK_CONTROL;
    parameters.arg1 = arg1;
    parameters.arg2 = 0;
    parameters.arg3 = 0;
    parameters.arg4 = 0;
    parameters.arg5 = 0;
    parameters.arg6 = 0;
    parameters.client_id = 0;
    parameters.secure_os_id = 0;

    status = zx_smc_call(get_root_resource(), &parameters, &result);
    if (status != ZX_OK) {
        UFS_LOGE("Failed to send smcall %d\n", status);
    }
}

static inline void ufs_hyper_mtk_smc(unsigned long arg1, unsigned long arg2)
{
    zx_status_t status;
    zx_smc_result_t result;
    zx_smc_parameters_t parameters;

    parameters.func_id = MTK_SIP_HYP_UFS_CONTROL;
    parameters.arg1 = arg1;
    parameters.arg2 = arg2;
    parameters.arg3 = 0;
    parameters.arg4 = 0;
    parameters.arg5 = 0;
    parameters.arg6 = 0;
    parameters.client_id = 0;
    parameters.secure_os_id = 0;

    status = zx_smc_call(get_root_resource(), &parameters, &result);
    if (status != ZX_OK) {
        UFS_LOGE("Failed to send smcall %d\n", status);
    }
}

/* Sip kernel interface */
#define ufs_mtk_va09_pwr_ctrl(on) \
	ufs_mtk_smc(UFS_MTK_SIP_VA09_PWR_CTRL, on, 0)

#define ufs_mtk_crypto_ctrl(enable) \
	ufs_mtk_smc(UFS_MTK_SIP_CRYPTO_CTRL, enable, 0)

#define ufs_mtk_ref_clk_notify(on, stage) \
	ufs_mtk_smc(UFS_MTK_SIP_REF_CLK_NOTIFICATION, on, stage)

#define ufs_mtk_device_reset_ctrl(high) \
	ufs_mtk_smc(UFS_MTK_SIP_DEVICE_RESET, high, 0)

#define ufs_mtk_sram_pwr_ctrl(on) \
	ufs_mtk_smc(UFS_MTK_SIP_SRAM_PWR_CTRL, on, 0)

#define ufs_mtk_get_vcc_num(void) \
	ufs_mtk_smc(UFS_MTK_SIP_GET_VCC_NUM, 0, 0)

#define ufs_mtk_device_pwr_ctrl(on, ufs_version) \
	ufs_mtk_smc(UFS_MTK_SIP_DEVICE_PWR_CTRL, on, ufs_version)

#define ufs_mtk_mphy_ctrl(op) \
	ufs_mtk_smc(UFS_MTK_SIP_MPHY_CTRL, op, 0)

#define ufs_mtk_mtcmos_ctrl(op) \
	ufs_mtk_smc(UFS_MTK_SIP_MTCMOS_CTRL, op, 0)

#define ufs_mtk_rpmb_key(region, pos) \
	ufs_mtk_smc(UFS_MTK_SIP_RPMB_KEY, region, pos)

#define ufs_mtk_vreg_ctrl(on) \
	ufs_mtk_smc(UFS_MTK_SIP_PWR_CTRL, on, 0)
