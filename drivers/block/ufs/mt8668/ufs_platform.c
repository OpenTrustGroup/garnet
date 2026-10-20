// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ufs_core.h"
#include "ufs_hcd.h"
#include "ufs_interface.h"
#include "ufs_platform.h"
#include "ufs_utils.h"

void ufs_platform_init(void)
{
    // Initialize platform-specific components here
}

/* UFS clocks as declared in DTS (auto8668p1_64_uos_evb.dts):
 *   <&cksys_reg_clk  CLK_CKSYS_REG_UFS_SEL>,
 *   <&cksys_reg_clk  CLK_CKSYS_REG_AES_UFSFDE_SEL>,
 *   <&cksys_reg_clk  CLK_CKSYS_REG_UFS_MBIST_SEL>,
 *   <&ufscfg_ao_clk  CLK_UFSAO_UNIPRO_TX_SYM_UFS>,
 *   <&ufscfg_ao_clk  CLK_UFSAO_UNIPRO_RX_SYM0_UFS>,
 *   <&ufscfg_ao_clk  CLK_UFSAO_UNIPRO_RX_SYM1_UFS>,
 *   <&ufscfg_ao_clk  CLK_UFSAO_UNIPRO_SYS_UFS>,
 *   <&ufscfg_ao_clk  CLK_UFSAO_UNIPRO_SAP_CFG_UFS>,
 *   <&ufscfg_ao_clk  CLK_UFSAO_UFS_PHY_TOP_AHB_S_BUS_UFS>,
 *   <&ufscfg_pdn_clk CLK_UFSPDN_UFSHCI_UFS_UFS>,
 *   <&ufscfg_pdn_clk CLK_UFSPDN_UFSHCI_AES_UFS>,
 *   <&ufscfg_pdn_clk CLK_UFSPDN_UFSHCI_UFS_AHB_UFS>,
 *   <&ufscfg_pdn_clk CLK_UFSPDN_UFSHCI_UFS_AXI_UFS>;
 *
 * Parent->child topology (see clk-mt6881-cg-ivi.c):
 *   L1 TOPCKGEN muxes (root, drive cksys_ufs_ck / cksys_aes_ufsfde_ck / MBIST)
 *   L2 *_UFS aliases (UFSCFG_AO / UFSCFG_PDN)
 *      Each alias's parent is the in-domain bare gate; ATF clk-smc handler
 *      enables the bare gate implicitly when the alias is enabled.
 *
 * Enable strictly L1 -> L2; disable in reverse so no child gate stays on
 * after its parent root mux is torn down.
 *
 * clk_id values come from include/dt-bindings/clock/mt6881-clk.h.
 */
static const struct ufs_clk_desc ufs_clk_list[] = {
    /* ---- L1: TOPCKGEN muxes (3) ---- */
    { CLK_SUBSYS_TOPCKGEN,    34, "cksys_ufs_sel" },
    { CLK_SUBSYS_TOPCKGEN,    33, "cksys_aes_ufsfde_sel" },
    { CLK_SUBSYS_TOPCKGEN,    35, "cksys_ufs_mbist_sel" },

    /* ---- L2: UFSCFG_AO _UFS aliases (6) ---- */
    { CLK_SUBSYS_UFSCFG_AO,    1, "ufsao_unipro_tx_sym_ufs" },
    { CLK_SUBSYS_UFSCFG_AO,    3, "ufsao_unipro_rx_sym0_ufs" },
    { CLK_SUBSYS_UFSCFG_AO,    5, "ufsao_unipro_rx_sym1_ufs" },
    { CLK_SUBSYS_UFSCFG_AO,    7, "ufsao_unipro_sys_ufs" },
    { CLK_SUBSYS_UFSCFG_AO,    9, "ufsao_unipro_sap_cfg_ufs" },
    { CLK_SUBSYS_UFSCFG_AO,   11, "ufsao_ufs_phy_ahb_s_bus_ufs" },

    /* ---- L2: UFSCFG_PDN _UFS aliases (4) ---- */
    { CLK_SUBSYS_UFSCFG_PDN,   1, "ufspdn_ufshci_ufs_ufs" },
    { CLK_SUBSYS_UFSCFG_PDN,   3, "ufspdn_ufshci_aes_ufs" },
    { CLK_SUBSYS_UFSCFG_PDN,   5, "ufspdn_ufshci_ufs_ahb_ufs" },
    { CLK_SUBSYS_UFSCFG_PDN,   7, "ufspdn_ufshci_ufs_axi_ufs" },
};

size_t ufs_mtk_get_clk_list(const struct ufs_clk_desc **clk_list)
{
    *clk_list = ufs_clk_list;
    return sizeof(ufs_clk_list) / sizeof(ufs_clk_list[0]);
}

static inline void ufs_mtk_mphy_set_bit(struct ufs_hba *hba, u32 reg, u32 bit)
{
    u32 val;

    if (!hba->mphy_base)
        return;

    val = ufsphy_readl(hba, reg);
    ufsphy_writel(hba, val | bit, reg);
}

static inline void ufs_mtk_mphy_clr_bit(struct ufs_hba *hba, u32 reg, u32 bit)
{
    u32 val;

    if (!hba->mphy_base)
        return;

    val = ufsphy_readl(hba, reg);
    ufsphy_writel(hba, val & ~bit, reg);
}

void ufs_mtk_mphy_set_active(struct ufs_hba *hba)
{
    if (!hba->mphy_base)
        return;

    ufs_mtk_mphy_set_bit(hba, MP_GLB_DIG_8C, PLL_PWR_ON);
    ufs_mtk_mphy_clr_bit(hba, MP_GLB_DIG_8C, FRC_FRC_PWR_ON);

    ufs_mtk_mphy_clr_bit(hba, MP_GLB_DIG_8C, PLL_ISO_EN);
    ufs_mtk_mphy_clr_bit(hba, MP_GLB_DIG_8C, FRC_PLL_ISO_EN);

    ufs_mtk_mphy_set_bit(hba, MP_LN_RX_44, CDR_PWR_ON);
    ufs_mtk_mphy_clr_bit(hba, MP_LN_RX_44, FRC_CDR_PWR_ON);

    ufs_mtk_mphy_clr_bit(hba, MP_LN_RX_44, CDR_ISO_EN);
    ufs_mtk_mphy_clr_bit(hba, MP_LN_RX_44, FRC_CDR_ISO_EN);

    ufs_mtk_mphy_set_bit(hba, MP_LN_DIG_RX_AC, RX_SQ_EN);
    ufs_mtk_mphy_clr_bit(hba, MP_LN_DIG_RX_AC, FRC_RX_SQ_EN);

    udelay(1);

    ufs_mtk_mphy_clr_bit(hba, MP_LN_DIG_RX_9C, FSM_DIFZ_FRC);
}

void ufs_mtk_mphy_set_deep_hibern(struct ufs_hba *hba)
{
    if (!hba->mphy_base)
        return;

    ufs_mtk_mphy_set_bit(hba, MP_LN_DIG_RX_9C, FSM_DIFZ_FRC);

    ufs_mtk_mphy_set_bit(hba, MP_LN_DIG_RX_AC, FRC_RX_SQ_EN);
    ufs_mtk_mphy_clr_bit(hba, MP_LN_DIG_RX_AC, RX_SQ_EN);

    ufs_mtk_mphy_set_bit(hba, MP_LN_RX_44, FRC_CDR_ISO_EN);
    ufs_mtk_mphy_set_bit(hba, MP_LN_RX_44, CDR_ISO_EN);

    ufs_mtk_mphy_set_bit(hba, MP_LN_RX_44, FRC_CDR_PWR_ON);
    ufs_mtk_mphy_clr_bit(hba, MP_LN_RX_44, CDR_PWR_ON);

    ufs_mtk_mphy_set_bit(hba, MP_GLB_DIG_8C, FRC_PLL_ISO_EN);
    ufs_mtk_mphy_set_bit(hba, MP_GLB_DIG_8C, PLL_ISO_EN);

    ufs_mtk_mphy_set_bit(hba, MP_GLB_DIG_8C, FRC_FRC_PWR_ON);
    ufs_mtk_mphy_clr_bit(hba, MP_GLB_DIG_8C, PLL_PWR_ON);
}
