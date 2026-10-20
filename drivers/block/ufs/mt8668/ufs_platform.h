/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copyright (c) 2020 MediaTek Inc.
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#pragma once
#include "ufs_sys.h"
struct ufs_hba;
struct ufs_clk_desc {
    uint32_t pvd;
    uint32_t id;
    const char* name;
};

enum {
    REG_UFS_PERICFG             = 0x448,
    REG_UFS_PERICFG_RST_N_BIT   = 3,
    REG_UFS_PERICFG_LDO_N_BIT   = 0xF,
    REG_UFS_PERICFG_LP_N_BIT    = 0xF
};

#define TRAP_UFS_FIRST  (0x3U << 17) /* [18:17] 0: UFS, 1: eMMC, 2: SF */

#define GPIO_BANK_BASE (GPIO_BASE + 0x000006F0)

#define EFUSE_VERSION  (EFUSE_BASE + 0x07CC)

#define UFS_MPHY_MP_GLB_DIG_58     (UFS_MPHY_BASE + 0x0058)
#define UFS_MPHY_MP_GLB_ANA_04     (UFS_MPHY_BASE + 0x1004)
#define UFS_MPHY_MP_LN_DIG_RX_08_1 (UFS_MPHY_BASE + 0xA108)
#define UFS_MPHY_MP_LN_DIG_RX_1C_1 (UFS_MPHY_BASE + 0xA11C)
#define UFS_MPHY_MP_LN_DIG_RX_20_1 (UFS_MPHY_BASE + 0xA120)
#define UFS_MPHY_MP_LN_DIG_RX_48_1 (UFS_MPHY_BASE + 0xA148)
#define UFS_MPHY_MP_LN_DIG_RX_80_1 (UFS_MPHY_BASE + 0xA180)
#define UFS_MPHY_MP_LN_DIG_TX_30   (UFS_MPHY_BASE + 0x8030)
#define UFS_MPHY_MP_LN_DIG_TX_34   (UFS_MPHY_BASE + 0x8034)
#define UFS_MPHY_MP_LN_DIG_TX_04_1 (UFS_MPHY_BASE + 0x8104)
#define UFS_MPHY_MP_LN_DIG_TX_30_1 (UFS_MPHY_BASE + 0x8130)
#define UFS_MPHY_MP_LN_DIG_TX_34_1 (UFS_MPHY_BASE + 0x8134)
#define UFS_MPHY_MP_LN_DIG_TX_A4_1 (UFS_MPHY_BASE + 0x81A4)

#define MP_GLB_DIG_8C               0x008C
#define FRC_PLL_ISO_EN              UFS_BIT(8)
#define PLL_ISO_EN                  UFS_BIT(9)
#define FRC_FRC_PWR_ON              UFS_BIT(10)
#define PLL_PWR_ON                  UFS_BIT(11)

#define MP_LN_DIG_RX_9C             0xA09C
#define FSM_DIFZ_FRC                UFS_BIT(18)

#define MP_LN_DIG_RX_AC             0xA0AC
#define FRC_RX_SQ_EN                UFS_BIT(0)
#define RX_SQ_EN                    UFS_BIT(1)

#define MP_LN_RX_44                 0xB044
#define FRC_CDR_PWR_ON              UFS_BIT(17)
#define CDR_PWR_ON                  UFS_BIT(18)
#define FRC_CDR_ISO_EN              UFS_BIT(19)
#define CDR_ISO_EN                  UFS_BIT(20)

#define CLK_SUBSYS_TOPCKGEN        0UL
#define CLK_SUBSYS_UFSCFG_AO       5UL
#define CLK_SUBSYS_UFSCFG_PDN      6UL

extern void ufs_platform_init(void);
size_t ufs_mtk_get_clk_list(const struct ufs_clk_desc **clk_list);
void ufs_mtk_mphy_set_active(struct ufs_hba *hba);
void ufs_mtk_mphy_set_deep_hibern(struct ufs_hba *hba);
