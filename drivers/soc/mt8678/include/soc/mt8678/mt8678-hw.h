// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// UFS registers
// pwr/crg etc. already been initialized in previous OS(lk2),
// MPHY has been configured in preloader, no need to configure them.
#define UFS_HCI_MMIO_BASE      0x16810000 //0x11270000
#define UFS_HCI_MMIO_LEN       0x2a00 //0x2300

// UFS IRQ
#define UFS_IRQ                 137

#define MTK_DOG_MMIO_BASE       0x1c010000//0x10007008 //0x1c010000
#define MTK_DOG_MMIO_LEN        0x1000

//DISP register
#define DISP_DDP_MMIO_BASE      0x30000000
#define DISP_DDP_MMIO_LEN       0x10000000

// DISP IRQ
#define DISP_IRQ                (493)

// SPI IRQ
#define SPI_IRQ                (0)

// SPI register
#define SPI_MMIO_BASE          (0x10000000 + 0x06110000)
#define SPI_MMIO_LEN           0x100

// GPIO IRQ
#define GPIO_IRQ                (432)

// GPIO register
#define GPIO_MMIO_BASE          (0x1002d000)
#define GPIO_MMIO_LEN           0x1000