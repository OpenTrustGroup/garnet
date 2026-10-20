// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define IO_BASE                 0x10000000

// UFS registers
// pwr/crg etc. already been initialized in previous OS(lk2),
// MPHY has been configured in preloader, no need to configure them.
#define     UFS0_MPHY_BASE          (IO_BASE + 0x012A0000)
#define     UFS_HCI_MMIO_BASE       (IO_BASE + 0x012B0000)
#define     UFS0_AO_CONFIG_BASE     (IO_BASE + 0x012B8000)

#define UFS_HCI_MMIO_LEN       0x3000

#define UFS_IRQ                 200

#define MTK_DOG_MMIO_BASE       0x1c010000
#define MTK_8676_DOG_MMIO_BASE  0x1c00a000
#define MTK_DOG_MMIO_LEN        0x1000

//DISP register
#define DISP_OVLSYS0_MUTEX_BASE (IO_BASE + 0x04401000)
#define DISP_OVLSYS0_MUTEX_LEN  0x10000000//0x1000

#define DISP_IRQ                (418)

// SPI0
#define SPI0_MMIO_BASE           IO_BASE + 0x01010000
#define SPI0_MMIO_LEN            0x100
#define SPI0_IRQ                 (242)

// SPI1
#define SPI1_MMIO_BASE           IO_BASE + 0x01011000
#define SPI1_MMIO_LEN            0x100
#define SPI1_IRQ                 (243)

// SPI2
#define SPI2_MMIO_BASE           IO_BASE + 0x01012000
#define SPI2_MMIO_LEN            0x100
#define SPI2_IRQ                 (244)

// SPI3
#define SPI3_MMIO_BASE           IO_BASE + 0x01013000
#define SPI3_MMIO_LEN            0x100
#define SPI3_IRQ                 (245)

// SPI4
#define SPI4_MMIO_BASE           IO_BASE + 0x11014000
#define SPI4_MMIO_LEN            0x100
#define SPI4_IRQ                 (246)

// SPI5
#define SPI5_MMIO_BASE           IO_BASE + 0x01015000
#define SPI5_MMIO_LEN            0x100
#define SPI5_IRQ                 (247)

// SPI6
#define SPI6_MMIO_BASE           IO_BASE + 0x01016000
#define SPI6_MMIO_LEN            0x100
#define SPI6_IRQ                 (248)

// SPI7
#define SPI7_MMIO_BASE           IO_BASE + 0x01017000
#define SPI7_MMIO_LEN            0x100
#define SPI7_IRQ                 (249)

//GPIO
#define GPIO_MMIO_BASE          (IO_BASE + 0x00005000)
#define GPIO_IRQ                345
#define GPIO_MMIO_LEN           0x100000