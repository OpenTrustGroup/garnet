// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

__BEGIN_CDECLS;
#include <ddk/protocol/platform-defs.h>
//#define __DEBUG__

#define MAX_FBS 2

#define TARGET_FRAME_RATE 60
#define FRAME_TIMEOUT_MARGIN_MS 2

uint32_t frame_interval_ms = 1000 / TARGET_FRAME_RATE + FRAME_TIMEOUT_MARGIN_MS;
// ---------------------------------------------------------------------------
//  Register Field Access
// ---------------------------------------------------------------------------

#define DISP_REG_GET(dev, reg32) \
            readl((dev)->regs + (reg32))

#define DISP_REG_SET(dev, reg32, val) \
            writel(val, (dev)->regs + (reg32))

#define IO_BASE                             0x10000000
#define DISP_OVL0_2L_BASE                   (IO_BASE + 0x04402000)
#define DISP_OVL0_2L_SIZE                   0x1000

#define DISP_OVL0_OVL_INTEN                  0x4
#define DISP_OVL0_OVL_INTSTA                 0x8
#define DISP_OVL0_OVL_L0_ADDR                (0xF60 + 0x2000)
#define DISP_REG_OVL_L0_ADDR                 DISP_OVL0_OVL_L0_ADDR
#define DISP_REG_OVL_INTEN                   DISP_OVL0_OVL_INTEN
#define DISP_REG_OVLSYS0_MUTEX_INTEN         0x0
#define DISP_REG_OVLSYS0_MUTEX_INTSTA        0x4
#define DISP_REG_OVLSYS0_MUTEX15             0x200
#define DISP_REG_OVLSYS0_MUTEX15_CTL         0x20C
#define DISP_REG_OVLSYS0_MUTEX_CFG           0x8

#define OVL_IRQ_FRAME_DONE                   0x2
#define OVL_IRQ_STATUS_ERR                   0x1E0

__END_CDECLS;

