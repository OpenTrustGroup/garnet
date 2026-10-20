/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __I2C_H__
#define __I2C_H__

#include "i2c_env.h"

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef GENMASK
#define GENMASK(h, l) (((1U << ((h) - (l) + 1)) - 1) << (l))
#endif

#define I2CBIT(x) (0x1U << x)
#define I2C_MULTI_ADDR(CHN_BASE, CHN) (CHN_BASE + CHN * 0x100)
#define APDMA_MULTI_ADDR(CHN_BASE, CHN) ((CHN <= I2C_CH1) ? CHN_BASE : CHN_BASE + (CHN - 1) * 0x80)

#define I2C_OK		   0
#define ECOMPARE_I2C    1
#define EACK_ERR_I2C	   6   /*slave device no ack*/
#define ERR_I2C	   7   /*slave device no ack*/
#define EAGAIN_I2C	   11  /* try again */
#define ENOMEM_I2C      12  /* out of memory */
#define EINVAL_I2C	   22  /* invalid argument */
#define EOPNOTSUPP_I2C  95  /* operation not supported on transport endpoint */
#define ETIMEDOUT_I2C   110 /* connection timed out */
#define EREMOTEIO_I2C   121 /* remote I/O error */

#define TIMEOUT		2000 /* ms */
#define MAX_FIFO_LEN	16
#define DMA_TEST_LEN	32

#define SOURCE_CLK_26MHZ 26000  /* 26M */
#define SOURCE_CLK_130MHZ 130000  /* 26M */
#define SOURCE_CLK_124P8MHZ 124800  /* 124.8M */
#define I2C_CLK_DIV_1 1
#define I2C_CLK_DIV_5 5

#define I2C_CONFERR			(1 << 9)
#define I2C_BUS_ERR		(1 << 8)
#define I2C_IBI		(1 << 7)
#define I2C_DMA_ERR		(1 << 6)
#define I2C_TIMEOUT		(1 << 5)
#define I2C_RS_TRANSFER		(1 << 4)
#define I2C_ARB_LOST		(1 << 3)
#define I2C_HS_NACKERR		(1 << 2)
#define I2C_ACKERR		(1 << 1)
#define I2C_TRANSAC_COMP	(1 << 0)
#define I2C_TRANSAC_START	(1 << 0)
#define I2C_RS_MUL_CNFG		(1 << 15)
#define I2C_RS_MUL_TRIG		(1 << 14)
#define I2C_INTR_MASK_ALL	0x0000
#define I2C_DCM_DISABLE		0x0000
#define I2C_IO_CONFIG_OPEN_DRAIN 0x0003
#define I2C_IO_CONFIG_PUSH_PULL	0x0000
#define I2C_SOFT_RST		0x0001
#define I2C_HANDSHAKE_RST	0x0020
#define I2C_RELIABILITY		0x10
#define I2C_DMAACK_ENABLE	0x08
#define I2C_FIFO_ADDR_CLR	0x0001
#define I2C_FIFO_ADDR_CLR_MCH	0x0004
#define I2C_DELAY_LEN		0x0002
#define I2C_TIME_CLR_VALUE	0x0000
#define I2C_TIME_DEFAULT_VALUE	0x0003
#define I2C_WRRD_TRANAC_VALUE	0x0002
#define I2C_RD_TRANAC_VALUE	0x0001
#define I2C_SCL_MIS_COMP_VALUE	0x0000
#define I2C_CHN_CLR_FLAG	0x0000
#define I2C_EN_MULTI_USER	0x0002

#define I2C_DMA_CON_TX		0x0000
#define I2C_DMA_CON_RX		0x0001
#define I2C_DMA_ASYNC_MODE	0x0004
#define I2C_DMA_SKIP_CONFIG	0x0010
#define I2C_DMA_DIR_CHANGE	0x0200
#define I2C_DMA_START_EN	0x0001
#define I2C_DMA_INT_FLAG_NONE	0x0000
#define I2C_DMA_CLR_FLAG	0x0000
#define I2C_DMA_WARM_RST	0x0001
#define I2C_DMA_HARD_RST	0x0002
#define I2C_DMA_HANDSHAKE_RST	0x0004
#define I2C_MULTI_USER_EN	0x2

#define GDMA_START		(0x1 << 15)
#define GDMA_DREQ		(0x1 << 4)
#define GDMA_DINC		(0x1 << 3)
#define GDMA_SINC		(0x1 << 2)

#define MAX_SAMPLE_CNT_DIV	8
#define MAX_STEP_CNT_DIV	64
#define MAX_CLOCK_DIV		256
#define MAX_HS_STEP_CNT_DIV	8
#define I2C_STANDARD_MODE_BUFFER	(1000 / 2)
#define I2C_FAST_MODE_BUFFER		(300 / 2)
#define I2C_FAST_MODE_PLUS_BUFFER	(20 / 2)

/* I2C Frequency Modes */
#define I2C_MAX_STANDARD_MODE_FREQ	100
#define I2C_MAX_FAST_MODE_FREQ		400
#define I2C_MAX_FAST_MODE_PLUS_FREQ	1000
#define I2C_MAX_HIGH_SPEED_MODE_FREQ	3400

#define I2C_CONTROL_WRAPPER             (0x1 << 0)
#define I2C_CONTROL_RS                  (0x1 << 1)
#define I2C_CONTROL_DMA_EN              (0x1 << 2)
#define I2C_CONTROL_CLK_EXT_EN          (0x1 << 3)
#define I2C_CONTROL_DIR_CHANGE          (0x1 << 4)
#define I2C_CONTROL_ACKERR_DET_EN       (0x1 << 5)
#define I2C_CONTROL_TRANSFER_LEN_CHANGE (0x1 << 6)
#define I2C_CONTROL_DMAACK_EN           (0x1 << 8)
#define I2C_CONTROL_ASYNC_MODE          (0x1 << 9)

#define I2C_CH1_MCU_INTE_EN	(0x1 << 0)
#define I2C_CH2_MCU_INTE_EN	(0x1 << 1)
#define I2C_CH3_MCU_INTE_EN	(0x1 << 2)
#define I2C_CH4_MCU_INTE_EN	(0x1 << 3)

enum APDMA_REGS_OFFSET {
	OFFSET_INT_FLAG = 0x0,
	OFFSET_INT_EN = 0x04,
	OFFSET_EN = 0x08,
	OFFSET_RST = 0x0c,
	OFFSET_CON = 0x18,
	OFFSET_TX_MEM_ADDR = 0x1c,
	OFFSET_RX_MEM_ADDR = 0x20,
	OFFSET_TX_LEN = 0x24,
	OFFSET_RX_LEN = 0x28,
	OFFSET_TX_4G_MODE = 0x54,
	OFFSET_RX_4G_MODE = 0x58,
};

enum GDMA_REGS_OFFSET {
	OFFSET_GDMA_SRC = 0x0,
	OFFSET_GDMA_DST = 0x04,
	OFFSET_GDMA_WPPT = 0x08,
	OFFSET_GDMA_WPTO = 0x0c,
	OFFSET_GDMA_CNT = 0x10,
	OFFSET_GDMA_CON = 0x14,
	OFFSET_GDMA_START = 0x18,
	OFFSET_GDMA_INTSTA = 0x1C,
	OFFSET_GDMA_ACKINT = 0x20,
	OFFSET_GDMA_RLCT = 0x24,
	OFFSET_GDMA_LIMIT = 0x28,
};

enum i2c_trans_st_rs {
	I2C_TRANS_STOP = 0,
	I2C_TRANS_REPEATED_START,
};

enum mtk_trans_op {
	I2C_MASTER_WR = 1,
	I2C_MASTER_RD,
	I2C_MASTER_WRRD,
};

enum I2C_REGS_OFFSET {
	OFFSET_DATA_PORT = 0x0,
	OFFSET_SLAVE_ADDR = 0x4,
	OFFSET_SLAVE_ADDR_V1 = 0x94,
	OFFSET_INTR_MASK = 0x8,
	OFFSET_INTR_STAT = 0xC,
	OFFSET_CONTROL = 0x10,
	OFFSET_TRANSFER_LEN = 0x14,
	OFFSET_TRANSAC_LEN = 0x18,
	OFFSET_DELAY_LEN = 0x1C,
	OFFSET_TIMING = 0x20,
	OFFSET_START = 0x24,
	OFFSET_EXT_CONF = 0x28,
	OFFSET_LTIMING = 0x2c,
	OFFSET_HS = 0x30,
	OFFSET_IO_CONFIG = 0x34,
	OFFSET_FIFO_ADDR_CLR = 0x38,
	OFFSET_SDA_TIMING = 0x3c,
	OFFSET_MCU_INTR = 0x40,
	OFFSET_TRANSFER_LEN_AUX = 0x44,
	OFFSET_CLOCK_DIV = 0x48,
	OFFSET_SOFTRESET = 0x50,
	OFFSET_MULTI_DMA = 0x8c,
	OFFSET_SCL_MIS_COMP_POINT = 0x90,
	OFFSET_DEBUGSTAT = 0xe4,
	OFFSET_DEBUGCTRL = 0xe8,
	OFFSET_DEBUGFSM = 0xec,
	OFFSET_FIFO_STAT = 0xf4,
	OFFSET_FIFO_THRESH = 0xf8,
	OFFSET_DCM_EN = 0xf88,
};

enum i2c_bus_id {
	I2C0 = 0,
	I2C1,
	I2C2,
	I2C3,
	I2C4,
	I2C5,
	I2C6,
	I2C7,
	I2C8,
	I2C9,
	I2C10,
	I2C11,
	I2C12,
};

enum i2c_ch_id {
	I2C_CHN = 0,
	I2C_CH1,
	I2C_CH2,
	I2C_CH3,
	I2C_CH4,
};

struct mtk_i2c_ac_timing {
	unsigned short htiming;
	unsigned short ltiming;
	unsigned short hs;
	unsigned short ext;
	unsigned short inter_clk_div;
	unsigned short scl_hl_ratio;
	unsigned short hs_scl_hl_ratio;
	unsigned short sta_stop;
	unsigned short hs_sta_stop;
	unsigned short sda_timing;
};

struct i2c_driver_quirks {
	bool poll_en;
	bool apdma_sync;
	bool slv_addr_v1;
	bool channel_en;
	enum mtk_dma_type dma_type;
	enum i2c_ch_id multi_chn;
	enum i2c_ch_id max_chn_id;
	unsigned int src_clk_kHz;
};

struct i2c_msg {
	unsigned char addr;					   /* slave address */
	unsigned char flags;
#define I2C_M_TEN	0x0010  /* this is a ten bit chip address */
#define I2C_M_RD	0x0001  /* read data, from slave to master */
#define I2C_M_STOP	0x8000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NOSTART	0x4000  /* if I2C_FUNC_NOSTART */
#define I2C_M_REV_DIR_ADDR  0x2000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_IGNORE_NAK	0x1000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NO_RD_ACK	0x0800  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_RECV_LEN	0x0400  /* length will be first received byte */
	unsigned int len;						/* msg length */
	unsigned char *buf;						/* pointer to msg data */
	unsigned char *vbuf;					/* pointer to msg data */
};

struct mtk_i2c {
	unsigned int i2c_chn_base;		/* I2C_CHN base addr */
	// unsigned int i2c_multi_base;	/* i2c transfer channel  base addr */
	void *i2c_multi_base;			/* I2C base addr */
	void *pdmabase;	/* dma base address*/
	unsigned char	id;
	bool use_push_pull;		/* IO config push-pull mode */
	bool msg_complete;
	bool dma_en;
	unsigned short irq_stat;/* interrupt status */
	unsigned int speed;		/* The speed in transfer khz */
	unsigned int cur_speed;	/* The i2c current speed*/
	void *dma_wr_buf; /*For dma write buffer*/
	void *dma_rd_buf; /*For dma read buffer*/
	enum mtk_trans_op op;
	unsigned short timing_reg;
	unsigned short high_speed_reg;
	unsigned short ltiming_reg;
	unsigned char auto_restart;
	struct mtk_i2c_ac_timing ac_timing;
	struct i2c_driver_quirks *i2c_quirks;
#if (CURRENT_ENV == SCP_I2C || CURRENT_ENV == MCUPM_I2C)
	SemaphoreHandle_t sem_comp;
	SemaphoreHandle_t bus_lock;
	struct INTC_IRQ irq;	/* i2c interrupt Infr */
#elif (CURRENT_ENV == ADSP_I2C)
	SemaphoreHandle_t sem_comp;
	SemaphoreHandle_t bus_lock;
	unsigned int irq;
#else
#if (CURRENT_ENV == ATF_I2C)
	spinlock_t i2c_lock;
#endif
	unsigned int irq;
	bool bus_lock;
#endif
};

struct mtk_i2c *mtk_i2c_get_bus(unsigned char bus_num);

#endif

