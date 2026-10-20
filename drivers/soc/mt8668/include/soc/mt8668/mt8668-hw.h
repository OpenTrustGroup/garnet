// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// UFS registers
// pwr/crg etc. already been initialized in previous OS(lk2),
// MPHY has been configured in preloader, no need to configure them.
#define UFS_HCI_MMIO_BASE      0x112b0000
#define UFS_HCI_MMIO_LEN       0x2300
#define UFS_MPHY_MMIO_BASE     0x112a0000
#define UFS_MPHY_MMIO_LEN      0x10000

#define PDEV_DID_GRT_I2C            17 //i2c

// UFS IRQ
#define UFS_IRQ                 818

#define MTK_DOG_MMIO_BASE       0x1c00a000
#define MTK_DOG_MMIO_LEN        0x1000

//DISP register
#define DISP_DDP_MMIO_BASE      0x30000000
#define DISP_DDP_MMIO_LEN       0x10000000

// DISP IRQ
#define DISP_IRQ                (493)

// SPI0
#define SPI0_MMIO_BASE           0x11010800
#define SPI0_MMIO_LEN            0x100
#define SPI0_IRQ                 (619)

// SPI1
#define SPI1_MMIO_BASE           0x11011800
#define SPI1_MMIO_LEN            0x100
#define SPI1_IRQ                 (620)

// SPI2
#define SPI2_MMIO_BASE           0x11012800
#define SPI2_MMIO_LEN            0x100
#define SPI2_IRQ                 (621)

// SPI3
#define SPI3_MMIO_BASE           0x11013800
#define SPI3_MMIO_LEN            0x100
#define SPI3_IRQ                 (622)

// SPI4
#define SPI4_MMIO_BASE           0x11014800
#define SPI4_MMIO_LEN            0x100
#define SPI4_IRQ                 (623)

// SPI5
#define SPI5_MMIO_BASE           0x11015800
#define SPI5_MMIO_LEN            0x100
#define SPI5_IRQ                 (624)

// SPI6
#define SPI6_MMIO_BASE           0x11016800
#define SPI6_MMIO_LEN            0x100
#define SPI6_IRQ                 (625)

// SPI7
#define SPI7_MMIO_BASE           0x11017800
#define SPI7_MMIO_LEN            0x100
#define SPI7_IRQ                 (626)

// I2C0
#define I2C0_MMIO_BASE           0x11e00000
#define I2C0_MMIO_DMA_BASE       0x11300300
#define I2C0_MMIO_LEN            0x1000
#define I2C0_MMIO_DMA_LEN        0x80
#define I2C0_IRQ                 (560)

// I2C1
#define I2C1_MMIO_BASE           0x11e01000
#define I2C1_MMIO_DMA_BASE       0x11300380
#define I2C1_MMIO_LEN            0x1000
#define I2C1_MMIO_DMA_LEN        0x80
#define I2C1_IRQ                 (561)

// I2C2
#define I2C2_MMIO_BASE           0x11d70000
#define I2C2_MMIO_DMA_BASE       0x11300400
#define I2C2_MMIO_LEN            0x1000
#define I2C2_MMIO_DMA_LEN        0x100
#define I2C2_IRQ                 (562)

// I2C3
#define I2C3_MMIO_BASE           0x11e02000
#define I2C3_MMIO_DMA_BASE       0x11300500
#define I2C3_MMIO_LEN            0x1000
#define I2C3_MMIO_DMA_LEN        0x80
#define I2C3_IRQ                 (563)

// I2C4
#define I2C4_MMIO_BASE           0x11d71000
#define I2C4_MMIO_DMA_BASE       0x11300580
#define I2C4_MMIO_LEN            0x1000
#define I2C4_MMIO_DMA_LEN        0x100
#define I2C4_IRQ                 (564)

// I2C5
#define I2C5_MMIO_BASE           0x11e03000
#define I2C5_MMIO_DMA_BASE       0x11300680
#define I2C5_MMIO_LEN            0x1000
#define I2C5_MMIO_DMA_LEN        0x80
#define I2C5_IRQ                 (565)

// I2C6
#define I2C6_MMIO_BASE           0x11e04000
#define I2C6_MMIO_DMA_BASE       0x11300700
#define I2C6_MMIO_LEN            0x1000
#define I2C6_MMIO_DMA_LEN        0x80
#define I2C6_IRQ                 (566)

// I2C7
#define I2C7_MMIO_BASE           0x11d72000
#define I2C7_MMIO_DMA_BASE       0x11300780
#define I2C7_MMIO_LEN            0x1000
#define I2C7_MMIO_DMA_LEN        0x100
#define I2C7_IRQ                 (567)

// I2C8
#define I2C8_MMIO_BASE           0x11d73000
#define I2C8_MMIO_DMA_BASE       0x11300880
#define I2C8_MMIO_LEN            0x1000
#define I2C8_MMIO_DMA_LEN        0x100
#define I2C8_IRQ                 (568)

// I2C9
#define I2C9_MMIO_BASE           0x11d74000
#define I2C9_MMIO_DMA_BASE       0x11300980
#define I2C9_MMIO_LEN            0x1000
#define I2C9_MMIO_DMA_LEN        0x100
#define I2C9_IRQ                 (569)

// I2C10
#define I2C10_MMIO_BASE           0x11d75000
#define I2C10_MMIO_DMA_BASE       0x11300a80
#define I2C10_MMIO_LEN            0x1000
#define I2C10_MMIO_DMA_LEN        0x80
#define I2C10_IRQ                 (570)

// I2C11
#define I2C11_MMIO_BASE           0x11d76000
#define I2C11_MMIO_DMA_BASE       0x11300b00
#define I2C11_MMIO_LEN            0x1000
#define I2C11_MMIO_DMA_LEN        0x80
#define I2C11_IRQ                 (571)

// I2C12
#define I2C12_MMIO_BASE          0x11d77000
#define I2C12_MMIO_DMA_BASE      0x11300b80
#define I2C12_MMIO_LEN           0x1000
#define I2C12_MMIO_DMA_LEN       0x80
#define I2C12_IRQ                (572)

// GPIO IRQ
#define GPIO_IRQ                (432)

// GPIO register
#define GPIO_MMIO_BASE          (0x1002d000)
#define GPIO_MMIO_LEN           0x1000
