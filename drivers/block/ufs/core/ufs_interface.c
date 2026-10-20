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
#include "ufs_interface.h"
#include "ufs_sys.h"

int ufs_init(mtk_ufs_t* ufs)
{
    struct ufs_hba *hba = &ufs->hba;
    int ret;
    int i;

    UFS_LOGE("[UFS] ufs_init start\n");
    ufs_cfg_mode(ufs, UFS_MODE_DMA);
    mtx_init(&hba->lock, mtx_plain);
    ret = ufshcd_init(ufs);
    if (ret != UFS_ERR_NONE) {
        UFS_LOGE("[UFS] err: ufshcd_init failed\n");
        return ret;
    }

    /* get LU size */
    for (i = 0; i < UFS_LU_INTERNAL_CNT; i++) {
        ret = ufs_get_lu_size(hba, i, NULL, &(hba->blk_cnt[i]));

        if (ret) {
            UFS_LOGE("[UFS] err: ufs_get_lu_size(%d) fail, ret %d\n", i, ret);
            return ret;
        }
    }

    UFS_LOGI("[UFS] info: boot device found\n");

    UFS_LOGI("[UFS] info: ufs init OK\n");

    return ret;
}

int ufs_deinit(mtk_ufs_t* ufs)
{
    int ret = 0;
    struct ufs_hba *hba = &ufs->hba;

    /* disable auto-hibern8 and send power off notify */
    ufshcd_writel(hba, 0, REG_AHIT);
    ufs_poweroff_notify(hba);

    ufs_reset_device_low(hba);
    return ret;
}

