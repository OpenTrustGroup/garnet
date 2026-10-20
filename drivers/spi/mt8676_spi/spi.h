// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

__BEGIN_CDECLS;

#include <garnet/public/lib/grt-spi.h>

typedef unsigned long uintptr_t;
typedef uintptr_t addr_t;
typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

enum GPIO_PIN {
    GPIO_UNSUPPORTED = -1,

    GPIO0, GPIO1, GPIO2, GPIO3, GPIO4, GPIO5, GPIO6, GPIO7,
    GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, GPIO14, GPIO15,
    GPIO16, GPIO17, GPIO18, GPIO19, GPIO20, GPIO21, GPIO22, GPIO23,
    GPIO24, GPIO25, GPIO26, GPIO27, GPIO28, GPIO29, GPIO30, GPIO31,
    GPIO32, GPIO33, GPIO34, GPIO35, GPIO36, GPIO37, GPIO38, GPIO39,
    GPIO40, GPIO41, GPIO42, GPIO43, GPIO44, GPIO45, GPIO46, GPIO47,
    GPIO48, GPIO49, GPIO50, GPIO51, GPIO52, GPIO53, GPIO54, GPIO55,
    GPIO56, GPIO57, GPIO58, GPIO59, GPIO60, GPIO61, GPIO62, GPIO63,
    GPIO64, GPIO65, GPIO66, GPIO67, GPIO68, GPIO69, GPIO70, GPIO71,
    GPIO72, GPIO73, GPIO74, GPIO75, GPIO76, GPIO77, GPIO78, GPIO79,
    GPIO80, GPIO81, GPIO82, GPIO83, GPIO84, GPIO85, GPIO86, GPIO87,
    GPIO88, GPIO89, GPIO90, GPIO91, GPIO92, GPIO93, GPIO94, GPIO95,
    GPIO96, GPIO97, GPIO98, GPIO99, GPIO100, GPIO101, GPIO102, GPIO103,
    GPIO104, GPIO105, GPIO106, GPIO107, GPIO108, GPIO109, GPIO110, GPIO111,
    GPIO112, GPIO113, GPIO114, GPIO115, GPIO116, GPIO117, GPIO118, GPIO119,
    GPIO120, GPIO121, GPIO122, GPIO123, GPIO124, GPIO125, GPIO126, GPIO127,
    GPIO128, GPIO129, GPIO130, GPIO131, GPIO132, GPIO133, GPIO134, GPIO135,
    GPIO136, GPIO137, GPIO138, GPIO139, GPIO140, GPIO141, GPIO142, GPIO143,
    GPIO144, GPIO145, GPIO146, GPIO147, GPIO148, GPIO149, GPIO150, GPIO151,
    GPIO152, GPIO153, GPIO154, GPIO155, GPIO156, GPIO157, GPIO158, GPIO159,
    GPIO160, GPIO161, GPIO162, GPIO163, GPIO164, GPIO165, GPIO166, GPIO167,
    GPIO168, GPIO169, GPIO170, GPIO171, GPIO172, GPIO173, GPIO174, GPIO175,
    GPIO176, GPIO177, GPIO178, GPIO179, GPIO180, GPIO181, GPIO182, GPIO183,
    GPIO184, GPIO185, GPIO186, GPIO187, GPIO188, GPIO189, GPIO190, GPIO191,
    GPIO192, GPIO193, GPIO194, GPIO195, GPIO196, GPIO197, GPIO198, GPIO199,
    GPIO200, GPIO201, GPIO202, GPIO203, GPIO204, GPIO205, GPIO206, GPIO207,
    GPIO208, GPIO209, GPIO210, GPIO211, GPIO212, GPIO213, GPIO214, GPIO215,
    GPIO216, GPIO217, GPIO218, GPIO219, GPIO220, GPIO221, GPIO222, GPIO223,
    GPIO224, GPIO225, GPIO226, GPIO227, GPIO228, GPIO229, GPIO230, GPIO231,
    GPIO232, GPIO233, GPIO234, GPIO235, GPIO236, GPIO237, GPIO238, GPIO239,
    GPIO240, GPIO241, GPIO242, GPIO243, GPIO244, GPIO245, GPIO246, GPIO247,
    GPIO248, GPIO249, GPIO250, GPIO251, GPIO252, GPIO253, GPIO254, GPIO255,
    MT_GPIO_BASE_MAX
};

enum GPIO_MODE {
    GPIO_MODE_UNSUPPORTED = -1,
    GPIO_MODE_GPIO  = 0,
    GPIO_MODE_00    = 0,
    GPIO_MODE_01    = 1,
    GPIO_MODE_02    = 2,
    GPIO_MODE_03    = 3,
    GPIO_MODE_04    = 4,
    GPIO_MODE_05    = 5,
    GPIO_MODE_06    = 6,
    GPIO_MODE_07    = 7,

    GPIO_MODE_MAX,
    GPIO_MODE_DEFAULT = GPIO_MODE_01,
    GPIO_MODE_INIT_NO_COVER = 0xFF,
};

enum GPIO_DIR {
    GPIO_DIR_UNSUPPORTED = -1,
    GPIO_DIR_IN     = 0,
    GPIO_DIR_OUT    = 1,

    GPIO_DIR_MAX,
    GPIO_DIR_DEFAULT = GPIO_DIR_IN,
};

/*----------------------------------------------------------------------------*/
/* GPIO OUTPUT */
enum GPIO_OUT {
    GPIO_OUT_UNSUPPORTED = -1,
    GPIO_OUT_ZERO = 0,
    GPIO_OUT_ONE  = 1,

    GPIO_OUT_MAX,
    GPIO_OUT_DEFAULT = GPIO_OUT_ZERO,
    GPIO_DATA_OUT_DEFAULT = GPIO_OUT_ZERO,
};
/*----------------------------------------------------------------------------*/
/* GPIO INPUT */
enum GPIO_IN {
    GPIO_IN_UNSUPPORTED = -1,
    GPIO_IN_ZERO = 0,
    GPIO_IN_ONE  = 1,

    GPIO_IN_MAX,
};

typedef struct grt_spi_data {
    io_buffer_t tx_buf;
    io_buffer_t rx_buf;
    void* tx_vaddr;
    zx_paddr_t tx_paddr;
    void* rx_vaddr;
    zx_paddr_t rx_paddr;
    grt_spi_transfer_head head;
    int size;
} grt_spi_data_t;

typedef struct spi_device {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    void* regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;
    zx_handle_t bti;
    mtx_t lock;
    // event to notify which group is active
    zx_handle_t event;

    bool stop_transfer;
    int bus_id;
} spi_device_t;

typedef struct spi_client_device {
    spi_device_t* spi;
    zx_device_t* device;
    grt_spi_data_t *data;
    grt_spi_cs_t *spi_cs;

    uintptr_t tx_addr;
    uintptr_t rx_addr;

    zx_handle_t tx_vmo;
    zx_handle_t rx_vmo;
} spi_client_device_t;

struct mtk_spi_bus {
    void* base;
    spi_device_t* dev;
};

// #define DUMP_SPI_DATA
//#define DUMP_SPI_REG
// #define SPI_DATA_TRANSFER 0xA0000001

#define MTK_SPI_ROUNDUP_DIV(a, b) (((a) + ((b)-1)) / (b))

#define MAX_SPI_BUS 8

#define SUPPORT_MODE_MWR
#define REGSET                 4
#define REGCLR                 8
#define REGMWR                 0xC

#define MAX_GPIO_PIN           232
#define GPIO_MODE_BITS         4
#define MAX_GPIO_MODE_PER_REG  8
#define MAX_GPIO_REG_BITS      32

#define RSUCCESS        0
#define ERACCESS        1
#define ERINVAL         2
#define ERWRAPPER       3

#define IO_BASE 0x10000000
#define GPIO_BASE           (IO_BASE + 0x00005000)
#define GPIO_DIR_BASE       (GPIO_BASE + 0x0)
#define GPIO_DOUT_BASE      (GPIO_BASE + 0x100)
#define GPIO_DIN_BASE       (GPIO_BASE + 0x200)
#define GPIO_MODE_BASE      (GPIO_BASE + 0x300)
#define GPIO_SEC_EN_BASE    (GPIO_BASE + 0xA00)

#define FUN_GPIO    GPIO_MODE_00
#define FUN_SPI     GPIO_MODE_01

#define DIR_OUT     GPIO_DIR_OUT
#define OUT_HIGH    GPIO_OUT_ONE
#define OUT_LOW     GPIO_OUT_ZERO

typedef struct {
    int miso;
    int cs;
    int mosi;
    int clk;
} spi_gpio_group_t;

static const spi_gpio_group_t spi_gpio_groups[MAX_SPI_BUS] = {
    // SPI0
    { .miso = GPIO48, .cs = GPIO47, .mosi = GPIO49, .clk = GPIO46 },
    // SPI1
    { .miso = GPIO52, .cs = GPIO51, .mosi = GPIO53, .clk = GPIO50 },
    // SPI2
    { .miso = GPIO13, .cs = GPIO12, .mosi = GPIO14, .clk = GPIO11},
    // SPI3
    { .miso = GPIO29, .cs = GPIO28, .mosi = GPIO30, .clk = GPIO27},
    // SPI4
    { .miso = GPIO38, .cs = GPIO37, .mosi = GPIO39, .clk = GPIO36},
    // SPI5
    { .miso = GPIO6, .cs = GPIO152, .mosi = GPIO5, .clk = GPIO151},
    // SPI6
    { .miso = GPIO97, .cs = GPIO99, .mosi = GPIO100, .clk = GPIO98},
    // SPI7
    { .miso = GPIO56, .cs = GPIO55, .mosi = GPIO57, .clk = GPIO54},
};

#define udelay(x) zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
#define DIV_ROUND_UP(n, d) (((n) + ((d) - 1)) / (d))

enum {
    SPI_HZ = 208000000,
    MTK_FIFO_DEPTH = 32,
    MTK_PACKET_SIZE = 65536,
    MTK_TXRX_TIMEOUT_US = 10*1000*1000,
    MTK_ARBITRARY_VALUE = 0xdadadada
};

enum {
    MTK_SPI_IDLE = 0,
    MTK_SPI_PAUSE_IDLE = 1
};

enum {
    MTK_SPI_BUSY_STATUS = 0x01,
    MTK_SPI_PAUSE_FINISH_INT_STATUS = 3
};

/* SPI_CFG0_REG */
enum {
    SPI_ADJUST_CFG0_CS_HOLD_OFFSET = 0,
    SPI_ADJUST_CFG0_CS_SETUP_OFFSET = 16,
};

/*SPI_CFG1_REG*/
enum {
    SPI_CFG1_CS_IDLE_SHIFT = 0,
    SPI_CFG1_PACKET_LOOP_SHIFT = 8,
    SPI_CFG1_PACKET_LENGTH_SHIFT = 16,

    SPI_CFG1_CS_IDLE_MASK = 0xff << SPI_CFG1_CS_IDLE_SHIFT,
    SPI_CFG1_PACKET_LOOP_MASK = 0xff << SPI_CFG1_PACKET_LOOP_SHIFT,
    SPI_CFG1_PACKET_LENGTH_MASK = 0x3ff << SPI_CFG1_PACKET_LENGTH_SHIFT,
    SPI_CFG1_IPM_PACKET_LENGTH_MASK = 0xffff << SPI_CFG1_PACKET_LENGTH_SHIFT,
};

/*SPI_CFG2_REG*/
enum {
    SPI_ADJUST_CFG2_SCK_HIGH_OFFSET = 0,
    SPI_ADJUST_CFG2_SCK_LOW_OFFSET = 16,
};

enum {
    SPI_CMD_ACT_SHIFT = 0,
    SPI_CMD_RESUME_SHIFT = 1,
    SPI_CMD_RST_SHIFT = 2,
    SPI_CMD_PAUSE_EN_SHIFT = 4,
    SPI_CMD_DEASSERT_SHIFT = 5,
    SPI_CMD_SAMPLE_SEL_SHIFT = 6,
    SPI_CMD_CS_POL_SHIFT = 7,
    SPI_CMD_CPHA_SHIFT = 8,
    SPI_CMD_CPOL_SHIFT = 9,
    SPI_CMD_RX_DMA_SHIFT = 10,
    SPI_CMD_TX_DMA_SHIFT = 11,
    SPI_CMD_TXMSBF_SHIFT = 12,
    SPI_CMD_RXMSBF_SHIFT = 13,
    SPI_CMD_RX_ENDIAN_SHIFT = 14,
    SPI_CMD_TX_ENDIAN_SHIFT = 15,
    SPI_CMD_FINISH_IE_SHIFT = 16,
    SPI_CMD_PAUSE_IE_SHIFT = 17,
    SPI_CMD_CS_PIN_SEL_SHIFT = 18,
    SPI_CMD_NONIDLE_MODE_SHIFT = 19,
    SPI_CMD_SPIM_LOOP = 21,
    SPI_CMD_SPIM_TICK_DLY_SHIFT = 22,

    SPI_CMD_ACT_EN = 1 << SPI_CMD_ACT_SHIFT,
    SPI_CMD_RESUME_EN = 1 << SPI_CMD_RESUME_SHIFT,
    SPI_CMD_RST_EN = 1 << SPI_CMD_RST_SHIFT,
    SPI_CMD_PAUSE_EN = 1 << SPI_CMD_PAUSE_EN_SHIFT,
    SPI_CMD_DEASSERT_EN = 1 << SPI_CMD_DEASSERT_SHIFT,
    SPI_CMD_CS_POL_EN = 1 << SPI_CMD_CS_POL_SHIFT,
    SPI_CMD_CPHA_EN = 1 << SPI_CMD_CPHA_SHIFT,
    SPI_CMD_CPOL_EN = 1 << SPI_CMD_CPOL_SHIFT,
    SPI_CMD_RX_DMA_EN = 1 << SPI_CMD_RX_DMA_SHIFT,
    SPI_CMD_TX_DMA_EN = 1 << SPI_CMD_TX_DMA_SHIFT,
    SPI_CMD_TXMSBF_EN = 1 << SPI_CMD_TXMSBF_SHIFT,
    SPI_CMD_RXMSBF_EN = 1 << SPI_CMD_RXMSBF_SHIFT,
    SPI_CMD_RX_ENDIAN_EN = 1 << SPI_CMD_RX_ENDIAN_SHIFT,
    SPI_CMD_TX_ENDIAN_EN = 1 << SPI_CMD_TX_ENDIAN_SHIFT,
    SPI_CMD_FINISH_IE_EN = 1 << SPI_CMD_FINISH_IE_SHIFT,
    SPI_CMD_PAUSE_IE_EN = 1 << SPI_CMD_PAUSE_IE_SHIFT,
    SPI_CMD_CS_PIN_SEL_EN = 1 << SPI_CMD_CS_PIN_SEL_SHIFT,
    SPI_CMD_NONIDLE_MODE_EN = 1 << SPI_CMD_NONIDLE_MODE_SHIFT,
    SPI_CMD_SELF_LOOP_EN = 1 << SPI_CMD_SPIM_LOOP,
    SPI_CMD_SPIM_TICK_DLY_MASK = 0x7f << SPI_CMD_SPIM_TICK_DLY_SHIFT,
};

/*SPI_SLICE_EN_REG*/
enum {
    SPI_RX_SLICE_EN_SHIFT = 0,
    SPI_TX_SLICE_EN_SHIFT = 3,

    SPI_RX_SLICE_MASK = 0x7 << SPI_RX_SLICE_EN_SHIFT,
    SPI_TX_SLICE_MASK = 0x7 << SPI_TX_SLICE_EN_SHIFT,
};

/* SPI peripheral register map. */
#define SPI_CFG0_REG                      0x0000
#define SPI_CFG1_REG                      0x0004
#define SPI_TX_SRC_REG                    0x0008
#define SPI_RX_DST_REG                    0x000c
#define SPI_TX_DATA_REG                   0x0010
#define SPI_RX_DATA_REG                   0x0014
#define SPI_CMD_REG                       0x0018
#define SPI_STATUS0_REG                   0x001c
#define SPI_STATUS1_REG                   0x0020
#define SPI_PAD_SEL_REG                   0x0024
#define SPI_CFG2_REG                      0x0028
#define SPI_TX_SRC_REG_64                 0x002c
#define SPI_RX_DST_REG_64                 0x0030
#define SPI_IRQ_TIME32_REG                0x0038
#define SPI_IRQ_TIME64_REG                0x003c
#define SPI_CFG3_REG                      0x0040
#define SPI_DEBUG0_REG                    0x004c
#define SPI_DEBUG1_REG                    0x0050
#define SPI_SLICE_EN_REG                  0x005c
#define SPI_CFG5_REG                      0x0068

#define SPI_PACKET_LENGTH_LOW_MASK          0xffff
#define SPI_PACKET_LENGTH_HIGH_MASK         0xffff0000
#define SPI_PACKET_LENGTH_HIGH_OFFSET       16
#define SPI_CFG1_PACKET_LENGTH_OFFSET       16

__END_CDECLS;