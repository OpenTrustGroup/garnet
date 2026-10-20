// SPDX-License-Identifier: BSD-3-Clause

#include <zircon/compiler.h>
#include <ddk/io-buffer.h>
#include <zircon/types.h>
#include <soc/mt8668/mt8668-hw.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/device.h>
#include <garnet/public/lib/grt-i2c.h>
#include <threads.h>
#include <sync/completion.h>

#pragma once

__BEGIN_CDECLS;
#define MAX_I2C_BUS 13

extern zx_handle_t i2c_bti[MAX_I2C_BUS];
extern zx_handle_t i2c_dma_handler[MAX_I2C_BUS];
extern bool i2c_bus_enable[MAX_I2C_BUS];

struct i2c_in_hdr {
	uint8_t status;
};

typedef struct grt_i2c_data {
    io_buffer_t input_buf;
    void* input_buf_vaddr;
    zx_paddr_t input_buf_paddr;
    bool input_buf_valid;

    io_buffer_t output_buf;
    void* output_buf_vaddr;
    zx_paddr_t output_buf_paddr;
    bool output_buf_valid;

    uint32_t size;
	grt_i2c_transfer_head head;
} grt_i2c_data_t;

typedef struct i2c_device {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    void* regs;
    void* dma_regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;
    zx_handle_t bti;
    completion_t completion;
    mtx_t lock;
    mtx_t irq_lock;
    // event to notify which group is active
    thrd_t irq_thread;
    zx_handle_t event;
    zx_handle_t dma_event;
    zx_handle_t irq_handle;
    pdev_irq_t out_irq;
    uint32_t irq;
    bool stop_transfer;
    int id;
    int msg_count;
    uint64_t owner_seq_id;
    zx_time_t owner_last_activity_ns;
} i2c_device_t;

typedef struct i2c_client_device {
    i2c_device_t* i2c;
    zx_device_t* device;
    grt_i2c_data_t *data;

    uintptr_t input_buf_addr; // map vmo addr
    uintptr_t output_buf_addr; // map vmo addr

	zx_handle_t input_vmo;
    zx_handle_t output_vmo;
} i2c_client_device_t;


// #define DUMP_I2C_DATA
//#define DUMP_I2C_REG
// #define I2C_DATA_TRANSFER 0xA0000001

#define MTK_I2C_ROUNDUP_DIV(a, b) (((a) + ((b)-1)) / (b))

#define FUN_GPIO    GPIO_MODE_00
#define FUN_I2C     GPIO_MODE_01

#define DIR_OUT     GPIO_DIR_OUT
#define OUT_HIGH    GPIO_OUT_ONE
#define OUT_LOW     GPIO_OUT_ZERO

#define MTK_I2C_ROUNDUP_DIV(a, b) (((a) + ((b)-1)) / (b))

// #define I2C_BASE            IO_BASE + 0x01011000
#define udelay(x) zx_nanosleep(zx_deadline_after(ZX_USEC(1)));

enum {
    I2C_HZ = 208000000,
    MTK_FIFO_DEPTH = 32,
    MTK_PACKET_SIZE = 1024,
    MTK_TXRX_TIMEOUT_US = 10*1000*1000,
    MTK_ARBITRARY_VALUE = 0xdadadada
};

enum {
    MTK_I2C_IDLE = 0,
    MTK_I2C_PAUSE_IDLE = 1
};

enum {
    MTK_I2C_BUSY_STATUS = 0x01,
    MTK_I2C_PAUSE_FINISH_INT_STATUS = 3
};

/* I2C_CFG0_REG */
enum {
    I2C_ADJUST_CFG0_CS_HOLD_OFFSET = 0,
    I2C_ADJUST_CFG0_CS_SETUP_OFFSET = 16,
};

/*I2C_CFG1_REG*/
enum {
    I2C_CFG1_CS_IDLE_SHIFT = 0,
    I2C_CFG1_PACKET_LOOP_SHIFT = 8,
    I2C_CFG1_PACKET_LENGTH_SHIFT = 16,

    I2C_CFG1_CS_IDLE_MASK = 0xff << I2C_CFG1_CS_IDLE_SHIFT,
    I2C_CFG1_PACKET_LOOP_MASK = 0xff << I2C_CFG1_PACKET_LOOP_SHIFT,
    I2C_CFG1_PACKET_LENGTH_MASK = 0xffff << I2C_CFG1_PACKET_LENGTH_SHIFT,
};

/*I2C_CFG2_REG*/
enum {
    I2C_ADJUST_CFG2_SCK_HIGH_OFFSET = 0,
    I2C_ADJUST_CFG2_SCK_LOW_OFFSET = 16,
};

enum {
    I2C_CMD_ACT_SHIFT = 0,
    I2C_CMD_RESUME_SHIFT = 1,
    I2C_CMD_RST_SHIFT = 2,
    I2C_CMD_PAUSE_EN_SHIFT = 4,
    I2C_CMD_DEASSERT_SHIFT = 5,
    I2C_CMD_SAMPLE_SEL_SHIFT = 6,
    I2C_CMD_CS_POL_SHIFT = 7,
    I2C_CMD_CPHA_SHIFT = 8,
    I2C_CMD_CPOL_SHIFT = 9,
    I2C_CMD_RX_DMA_SHIFT = 10,
    I2C_CMD_TX_DMA_SHIFT = 11,
    I2C_CMD_TXMSBF_SHIFT = 12,
    I2C_CMD_RXMSBF_SHIFT = 13,
    I2C_CMD_RX_ENDIAN_SHIFT = 14,
    I2C_CMD_TX_ENDIAN_SHIFT = 15,
    I2C_CMD_FINISH_IE_SHIFT = 16,
    I2C_CMD_PAUSE_IE_SHIFT = 17,
    I2C_CMD_I2CM_LOOP = 21,
    I2C_CMD_I2CM_TICK_DLY_SHIFT = 22,

    I2C_CMD_ACT_EN = 1 << I2C_CMD_ACT_SHIFT,
    I2C_CMD_RESUME_EN = 1 << I2C_CMD_RESUME_SHIFT,
    I2C_CMD_RST_EN = 1 << I2C_CMD_RST_SHIFT,
    I2C_CMD_PAUSE_EN = 1 << I2C_CMD_PAUSE_EN_SHIFT,
    I2C_CMD_DEASSERT_EN = 1 << I2C_CMD_DEASSERT_SHIFT,
    I2C_CMD_CPHA_EN = 1 << I2C_CMD_CPHA_SHIFT,
    I2C_CMD_CPOL_EN = 1 << I2C_CMD_CPOL_SHIFT,
    I2C_CMD_RX_DMA_EN = 1 << I2C_CMD_RX_DMA_SHIFT,
    I2C_CMD_TX_DMA_EN = 1 << I2C_CMD_TX_DMA_SHIFT,
    I2C_CMD_TXMSBF_EN = 1 << I2C_CMD_TXMSBF_SHIFT,
    I2C_CMD_RXMSBF_EN = 1 << I2C_CMD_RXMSBF_SHIFT,
    I2C_CMD_RX_ENDIAN_EN = 1 << I2C_CMD_RX_ENDIAN_SHIFT,
    I2C_CMD_TX_ENDIAN_EN = 1 << I2C_CMD_TX_ENDIAN_SHIFT,
    I2C_CMD_FINISH_IE_EN = 1 << I2C_CMD_FINISH_IE_SHIFT,
    I2C_CMD_PAUSE_IE_EN = 1 << I2C_CMD_PAUSE_IE_SHIFT,
    I2C_CMD_SELF_LOOP_EN = 1 << I2C_CMD_I2CM_LOOP,
    I2C_CMD_I2CM_TICK_DLY_MASK = 0x7f << I2C_CMD_I2CM_TICK_DLY_SHIFT,
};

/*I2C_SLICE_EN_REG*/
enum {
    I2C_RX_SLICE_EN_SHIFT = 0,
    I2C_TX_SLICE_EN_SHIFT = 3,

    I2C_RX_SLICE_MASK = 0x7 << I2C_RX_SLICE_EN_SHIFT,
    I2C_TX_SLICE_MASK = 0x7 << I2C_TX_SLICE_EN_SHIFT,
};

struct mtk_i2c_bus {
    void *base;
    void *pdma_base;
};

struct mtk_i2c_vmo {
	zx_handle_t input_vmo;
    zx_handle_t output_vmo;
    int size;
};

extern struct mtk_i2c_vmo vmo[MAX_I2C_BUS];
extern struct mtk_i2c_bus bus[MAX_I2C_BUS];

typedef unsigned long uintptr_t;
typedef uintptr_t addr_t;
typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

__END_CDECLS;
