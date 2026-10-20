// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ufs_core.h"
#include "ufs_interface.h"
#include "ufs_platform.h"
#include "ufs_utils.h"

void ufs_platform_init(void)
{
    // Initialize platform-specific components here
}

size_t ufs_mtk_get_clk_list(const struct ufs_clk_desc **clk_list)
{
    *clk_list = NULL;
    return 0;
}

void ufs_mtk_mphy_set_active(struct ufs_hba *hba)
{
    (void)hba;
}

void ufs_mtk_mphy_set_deep_hibern(struct ufs_hba *hba)
{
    (void)hba;
}
