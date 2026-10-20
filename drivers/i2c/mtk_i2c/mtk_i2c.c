/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "i2c_func.h"
#include "i2c_env.h"
#include "i2c_platform.h"
#include "../mt8668_i2c/i2c.h"
#if CURRENT_ENV == LK_I2C
#include <platform/mt_i2c.h>
#else
#include "mt_i2c.h"
#endif

struct mtk_i2c *mtk_i2c_get_bus(unsigned char bus_num);
static struct mtk_i2c mtk_i2c_busses[MTK_I2C_MAX_PORT_NUMBER];

/**
 * struct i2c_spec_values:
 * @min_low_ns: min LOW period of the SCL clock
 * @min_su_sta_ns: min set-up time for a repeated START condition
 * @max_hd_dat_ns: max data hold time
 * @min_su_dat_ns: min data set-up time
 */
struct i2c_spec_values {
	unsigned int min_low_ns;
	unsigned int min_su_sta_ns;
	unsigned int max_hd_dat_ns;
	unsigned int min_su_dat_ns;
};

static const struct i2c_spec_values standard_mode_spec = {
	.min_low_ns = 4700 + I2C_STANDARD_MODE_BUFFER,
	.min_su_sta_ns = 4700 + I2C_STANDARD_MODE_BUFFER,
	.max_hd_dat_ns = 3450 - I2C_STANDARD_MODE_BUFFER,
	.min_su_dat_ns = 250 + I2C_STANDARD_MODE_BUFFER,
};

static const struct i2c_spec_values fast_mode_spec = {
	.min_low_ns = 1300 + I2C_FAST_MODE_BUFFER,
	.min_su_sta_ns = 600 + I2C_FAST_MODE_BUFFER,
	.max_hd_dat_ns = 900 - I2C_FAST_MODE_BUFFER,
	.min_su_dat_ns = 100 + I2C_FAST_MODE_BUFFER,
};

static const struct i2c_spec_values fast_mode_plus_spec = {
	.min_low_ns = 500 + I2C_FAST_MODE_PLUS_BUFFER,
	.min_su_sta_ns = 260 + I2C_FAST_MODE_PLUS_BUFFER,
	.max_hd_dat_ns = 400 - I2C_FAST_MODE_PLUS_BUFFER,
	.min_su_dat_ns = 50 + I2C_FAST_MODE_PLUS_BUFFER,
};

static void mtk_i2c_write_8(struct mtk_i2c *i2c, unsigned short val,
			   enum I2C_REGS_OFFSET reg)
{
	mt_reg_sync_writeb(val, i2c->i2c_multi_base + reg);
}

static unsigned int mtk_i2c_read_8(struct mtk_i2c *i2c, enum I2C_REGS_OFFSET reg)
{
	return readl(i2c->i2c_multi_base + reg);
}

static void mtk_i2c_write_16(struct mtk_i2c *i2c, unsigned short val,
			   enum I2C_REGS_OFFSET reg)
{
	mt_reg_sync_writew(val, i2c->i2c_multi_base + reg);
}

static unsigned short mtk_i2c_read_16(struct mtk_i2c *i2c, enum I2C_REGS_OFFSET reg)
{
	return readl(i2c->i2c_multi_base + reg);
}

static void mtk_dma_write_32(struct mtk_i2c *i2c, unsigned int val, int reg_offset)
{
	mt_reg_sync_writel(val, i2c->pdmabase + reg_offset);
}

static unsigned int mtk_dma_read_32(struct mtk_i2c *i2c, int reg_offset)
{
	return readl(i2c->pdmabase + reg_offset);
}

struct mtk_i2c *mtk_i2c_get_bus(unsigned char bus_num)
{
	if (bus_num >= MTK_I2C_MAX_PORT_NUMBER) {
		I2CERR("Get I2C%d bus fail\n", bus_num);
		return NULL;
	}

	mtk_i2c_busses[bus_num].id = bus_num;

	return &mtk_i2c_busses[bus_num];
}

static int mtk_i2c_base_init(struct mtk_i2c *i2c)
{
	unsigned char multi_chn = 0;

	if (i2c == NULL)
		return -EINVAL_I2C;

	i2c->i2c_quirks = mtk_i2c_driver_quirks;
	multi_chn = (unsigned char)i2c->i2c_quirks[i2c->id].multi_chn;

	switch (i2c->id) {
	case I2C0:
		i2c->i2c_chn_base = I2C0_BASE_ADDR;
		i2c->pdmabase = APDMA_MULTI_ADDR(DMA0_BASE_ADDR, multi_chn);
		i2c->irq = MTK_I2C0_GIC_IRQ;
		break;
	case I2C1:
		i2c->i2c_chn_base = I2C1_BASE_ADDR;
		i2c->pdmabase = APDMA_MULTI_ADDR(DMA1_BASE_ADDR, multi_chn);
		i2c->irq = MTK_I2C1_GIC_IRQ;
		break;
	case I2C2:
		i2c->i2c_chn_base = I2C2_BASE_ADDR;
		i2c->pdmabase = APDMA_MULTI_ADDR(DMA2_BASE_ADDR, multi_chn);
		i2c->irq = MTK_I2C2_GIC_IRQ;
		break;
	case I2C3:
		i2c->i2c_chn_base = I2C3_BASE_ADDR;
		i2c->pdmabase = APDMA_MULTI_ADDR(DMA3_BASE_ADDR, multi_chn);
		i2c->irq = MTK_I2C3_GIC_IRQ;
		break;
	case I2C4:
		i2c->i2c_chn_base = I2C4_BASE_ADDR;
		i2c->pdmabase = APDMA_MULTI_ADDR(DMA4_BASE_ADDR, multi_chn);
		i2c->irq = MTK_I2C4_GIC_IRQ;
		break;
	case I2C5:
		i2c->i2c_chn_base = I2C5_BASE_ADDR;
		i2c->pdmabase = APDMA_MULTI_ADDR(DMA5_BASE_ADDR, multi_chn);
		i2c->irq = MTK_I2C5_GIC_IRQ;
		break;
	default:
		I2CERR("mtk_i2c_base_init, invalid para: i2c->id=%d\n", i2c->id);
		return -EINVAL_I2C;
	}

	i2c->i2c_multi_base = I2C_MULTI_ADDR(i2c->i2c_chn_base, multi_chn);

	return 0;
}

static void mtk_i2c_init_hw(struct mtk_i2c *i2c)
{
	unsigned short control_reg;
	unsigned short intr_stat_reg;

	i2c->i2c_multi_base = bus[i2c->id].base;
	i2c->pdmabase = bus[i2c->id].pdma_base;
	mtk_i2c_write_16(i2c, I2C_CHN_CLR_FLAG, OFFSET_START);
	intr_stat_reg = mtk_i2c_read_16(i2c, OFFSET_INTR_STAT);
	mtk_i2c_write_16(i2c, intr_stat_reg, OFFSET_INTR_STAT);

	/* Set ioconfig */
	if (i2c->use_push_pull)
		mtk_i2c_write_16(i2c, I2C_IO_CONFIG_PUSH_PULL, OFFSET_IO_CONFIG);
	else
		mtk_i2c_write_16(i2c, I2C_IO_CONFIG_OPEN_DRAIN, OFFSET_IO_CONFIG);
	
	mtk_i2c_write_16(i2c, i2c->timing_reg, OFFSET_TIMING);
	mtk_i2c_write_16(i2c, i2c->high_speed_reg, OFFSET_HS);
	mtk_i2c_write_16(i2c, i2c->ltiming_reg, OFFSET_LTIMING);
	mtk_i2c_write_16(i2c, i2c->ac_timing.ext, OFFSET_EXT_CONF);
	mtk_i2c_write_16(i2c, i2c->ac_timing.inter_clk_div, OFFSET_CLOCK_DIV);
	mtk_i2c_write_16(i2c, i2c->ac_timing.sda_timing, OFFSET_SDA_TIMING);
	mtk_i2c_write_16(i2c, i2c->ac_timing.hs, OFFSET_HS);
	mtk_i2c_write_16(i2c, i2c->ac_timing.htiming, OFFSET_TIMING);
	mtk_i2c_write_16(i2c, i2c->ac_timing.ltiming, OFFSET_LTIMING);

	control_reg = I2C_CONTROL_ACKERR_DET_EN | I2C_CONTROL_CLK_EXT_EN;
	mtk_i2c_write_16(i2c, control_reg, OFFSET_CONTROL);
	mtk_i2c_write_16(i2c, I2C_DELAY_LEN, OFFSET_DELAY_LEN);
}

static const struct i2c_spec_values *mtk_i2c_get_spec(unsigned int speed)
{
	if (speed <= I2C_MAX_STANDARD_MODE_FREQ)
		return &standard_mode_spec;
	else if (speed <= I2C_MAX_FAST_MODE_FREQ)
		return &fast_mode_spec;
	else
		return &fast_mode_plus_spec;
}

static int mtk_i2c_max_step_cnt(unsigned int target_speed)
{
	if (target_speed > I2C_MAX_FAST_MODE_PLUS_FREQ)
		return MAX_HS_STEP_CNT_DIV;
	else
		return MAX_STEP_CNT_DIV;
}

static int mtk_i2c_get_clk_div_restri(struct mtk_i2c *i2c,
				      unsigned int sample_cnt)
{
	int clk_div_restri = 0;

	if (sample_cnt == 1) {
		if (i2c->ac_timing.inter_clk_div == 0)
			clk_div_restri = 0;
		else
			clk_div_restri = 1;
	} else {
		if (i2c->ac_timing.inter_clk_div == 0)
			clk_div_restri = -1;
		else if (i2c->ac_timing.inter_clk_div == 1)
			clk_div_restri = 0;
		else
			clk_div_restri = 1;
	}

	return clk_div_restri;
}

/*
 * Check and Calculate i2c ac-timing
 *
 * Hardware design:
 * sample_ns = (1000000 * (sample_cnt + 1)) / clk_src
 * xxx_cnt_div =  spec->min_xxx_ns / sample_ns
 *
 * Sample_ns is rounded down for xxx_cnt_div would be greater
 * than the smallest spec.
 * The sda_timing is chosen as the middle value between
 * the largest and smallest.
 */
static int mtk_i2c_check_ac_timing(struct mtk_i2c *i2c,
				   unsigned int clk_src,
				   unsigned int check_speed,
				   unsigned int step_cnt,
				   unsigned int sample_cnt)
{
	const struct i2c_spec_values *spec;
	unsigned int su_sta_cnt, low_cnt, high_cnt, max_step_cnt;
	unsigned int sda_max, sda_min, clk_ns, max_sta_cnt = 0x3f;
	unsigned int sample_ns = (1000000 * (sample_cnt + 1)) / clk_src;

	max_sta_cnt = 0x100;

	clk_ns = 1000000 / clk_src;

	spec = mtk_i2c_get_spec(check_speed);

	su_sta_cnt = DIV_ROUND_UP(spec->min_su_sta_ns, clk_ns);
	if (su_sta_cnt > max_sta_cnt)
		return -1;

	low_cnt = DIV_ROUND_UP(spec->min_low_ns, sample_ns);
	max_step_cnt = mtk_i2c_max_step_cnt(check_speed);
	if ((2 * step_cnt) > low_cnt && low_cnt < max_step_cnt) {
		if (low_cnt > step_cnt) {
			high_cnt = 2 * step_cnt - low_cnt;
		} else {
			high_cnt = step_cnt;
			low_cnt = step_cnt;
		}
	} else {
		return -2;
	}

	sda_max = spec->max_hd_dat_ns / sample_ns;
	if (sda_max > low_cnt)
		sda_max = 0;

	sda_min = DIV_ROUND_UP(spec->min_su_dat_ns, sample_ns);
	if (sda_min < low_cnt)
		sda_min = 0;

	if (sda_min > sda_max)
		return -3;

	if (check_speed > I2C_MAX_FAST_MODE_PLUS_FREQ) {
		i2c->ac_timing.hs = I2C_TIME_DEFAULT_VALUE |
			(sample_cnt << 12) | (high_cnt << 8);
		i2c->ac_timing.ltiming &= ~GENMASK(15, 9);
		i2c->ac_timing.ltiming |= (sample_cnt << 12) |
			(low_cnt << 9);
		i2c->ac_timing.ext &= ~GENMASK(7, 1);
		i2c->ac_timing.ext |= (su_sta_cnt << 1) | (1 << 0);
		i2c->ac_timing.sda_timing &= ~GENMASK(11, 6);
		i2c->ac_timing.sda_timing |= (1 << 12) |
			((sda_max + sda_min) / 2) << 6;
	} else {
		i2c->ac_timing.htiming = (sample_cnt << 8) | (high_cnt);
		i2c->ac_timing.ltiming = (sample_cnt << 6) | (low_cnt);
		i2c->ac_timing.ext = (su_sta_cnt << 8) | (1 << 0);
		i2c->ac_timing.sda_timing = (1 << 12) |
			(sda_max + sda_min) / 2;
	}

	return 0;
}

/*
 * Calculate i2c port speed
 *
 * Hardware design:
 * i2c_bus_freq = parent_clk / (clock_div * 2 * sample_cnt * step_cnt)
 * clock_div: fixed in hardware, but may be various in different SoCs
 *
 * The calculation want to pick the highest bus frequency that is still
 * less than or equal to i2c->speed. The calculation try to get
 * sample_cnt and step_cn
 */
static int mtk_i2c_calculate_speed(struct mtk_i2c *i2c, unsigned int clk_src,
				   unsigned int target_speed,
				   unsigned int *timing_step_cnt,
				   unsigned int *timing_sample_cnt)
{
	unsigned int step_cnt;
	unsigned int sample_cnt;
	unsigned int max_step_cnt;
	unsigned int base_sample_cnt = MAX_SAMPLE_CNT_DIV;
	unsigned int base_step_cnt;
	unsigned int opt_div;
	unsigned int best_mul;
	unsigned int cnt_mul;
	int ret = 0;
	int clk_div_restri = 0;

	if (target_speed > I2C_MAX_HIGH_SPEED_MODE_FREQ)
		target_speed = I2C_MAX_HIGH_SPEED_MODE_FREQ;

	max_step_cnt = mtk_i2c_max_step_cnt(target_speed);
	base_step_cnt = max_step_cnt;
	/* Find the best combination */
	opt_div = DIV_ROUND_UP(clk_src >> 1, target_speed);
	best_mul = MAX_SAMPLE_CNT_DIV * max_step_cnt;

	/* Search for the best pair (sample_cnt, step_cnt) with
	 * 0 < sample_cnt < MAX_SAMPLE_CNT_DIV
	 * 0 < step_cnt < max_step_cnt
	 * sample_cnt * step_cnt >= opt_div
	 * optimizing for sample_cnt * step_cnt being minimal
	 */
	for (sample_cnt = 1; sample_cnt <= MAX_SAMPLE_CNT_DIV; sample_cnt++) {
		clk_div_restri = mtk_i2c_get_clk_div_restri(i2c, sample_cnt);
		step_cnt = DIV_ROUND_UP(opt_div + clk_div_restri, sample_cnt);
		cnt_mul = step_cnt * sample_cnt;
		if (step_cnt > max_step_cnt)
			continue;

		if (cnt_mul < best_mul) {
			ret = mtk_i2c_check_ac_timing(i2c, clk_src,
				target_speed, step_cnt - 1, sample_cnt - 1);
			if (ret)
				continue;

			best_mul = cnt_mul;
			base_sample_cnt = sample_cnt;
			base_step_cnt = step_cnt;
			if (best_mul == (opt_div + clk_div_restri))
				break;
		}
	}

	if (ret)
		return -EINVAL_I2C;

	sample_cnt = base_sample_cnt;
	step_cnt = base_step_cnt;

	/* In this case, hardware can't support such low i2c_bus_freq */
	if ((clk_src / (2 * (sample_cnt * step_cnt - clk_div_restri))) >
	    target_speed) {
		if (i2c->i2c_quirks[i2c->id].src_clk_kHz / clk_src == MAX_CLOCK_DIV)
			I2CERR("Unsupported speed (%ukHz)\n",	target_speed);

		return -EINVAL_I2C;
	}

	*timing_step_cnt = step_cnt - 1;
	*timing_sample_cnt = sample_cnt - 1;

	return 0;
}

static int mtk_i2c_set_speed(struct mtk_i2c *i2c)
{
	unsigned int clk_src;
	unsigned int step_cnt;
	unsigned int sample_cnt;
	unsigned int l_step_cnt;
	unsigned int l_sample_cnt;
	unsigned int target_speed;
	unsigned int clk_div;
	unsigned int max_clk_div;
	unsigned int parent_clk;
	struct i2c_driver_quirks *i2c_quirks = &i2c->i2c_quirks[i2c->id];
	int ret;

	if (i2c->cur_speed == i2c->speed)
		return 0;

	target_speed = i2c->speed;
	parent_clk = i2c_quirks->src_clk_kHz;
	max_clk_div = MAX_CLOCK_DIV;

	for (clk_div = 1; clk_div <= max_clk_div; clk_div++) {
		clk_src = parent_clk / clk_div;

		if (target_speed > I2C_MAX_FAST_MODE_PLUS_FREQ) {
			/* Set master code speed register */
			ret = mtk_i2c_calculate_speed(i2c, clk_src,
						      I2C_MAX_FAST_MODE_FREQ,
						      &l_step_cnt,
						      &l_sample_cnt);
			if (ret < 0)
				continue;

			i2c->timing_reg = (l_sample_cnt << 8) | l_step_cnt;

			/* Set the high speed mode register */
			ret = mtk_i2c_calculate_speed(i2c, clk_src,
						      target_speed, &step_cnt,
						      &sample_cnt);
			if (ret < 0)
				continue;

			i2c->high_speed_reg = I2C_TIME_DEFAULT_VALUE |
					(sample_cnt << 12) | (step_cnt << 8);
			i2c->ltiming_reg = (l_sample_cnt << 6) | l_step_cnt |
					(sample_cnt << 12) | (step_cnt << 9);
		} else {
			ret = mtk_i2c_calculate_speed(i2c, clk_src,
						      target_speed, &l_step_cnt,
						      &l_sample_cnt);
			if (ret < 0)
				continue;

			/* Disable the high speed transaction */
			i2c->high_speed_reg = I2C_TIME_CLR_VALUE;
			i2c->timing_reg = (l_sample_cnt << 8) | l_step_cnt;
			i2c->ltiming_reg = (l_sample_cnt << 6) | l_step_cnt;

		}

		break;
	}

	i2c->ac_timing.inter_clk_div = clk_div - 1;
	i2c->cur_speed = i2c->speed;

	return 0;
}

static void mtk_apdma_dump(struct mtk_i2c *i2c)
{
	I2CERR("APDMA Base-address: 0x%x\n", i2c->pdmabase);
	I2CERR("INT_FLAG: 0x%x, INT_EN: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_INT_FLAG),
		mtk_dma_read_32(i2c, OFFSET_INT_EN));
	I2CERR("EN: 0x%x, RST: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_EN),
		mtk_dma_read_32(i2c, OFFSET_RST));
	I2CERR("CON: 0x%x, TX_MEM_ADDR: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_CON),
		mtk_dma_read_32(i2c, OFFSET_TX_MEM_ADDR));
	I2CERR("RX_MEM_ADDR: 0x%x, TX_LEN: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_RX_MEM_ADDR),
		mtk_dma_read_32(i2c, OFFSET_TX_LEN));
	I2CERR("RX_LEN: 0x%x, TX_4G_MODE: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_RX_LEN),
		mtk_dma_read_32(i2c, OFFSET_TX_4G_MODE));
	I2CERR("RX_4G_MODE: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_RX_4G_MODE));
}

static void mtk_gdma_dump(struct mtk_i2c *i2c)
{
	I2CERR("GDMA Base-address: 0x%x\n", i2c->pdmabase);
	I2CERR("GDMA_SRC: 0x%x, GDMA_DST: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_GDMA_SRC),
		mtk_dma_read_32(i2c, OFFSET_GDMA_DST));
	I2CERR("GDMA_WPPT: 0x%x, GDMA_WPTO: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_GDMA_WPPT),
		mtk_dma_read_32(i2c, OFFSET_GDMA_WPTO));
	I2CERR("GDMA_CNT: 0x%x, GDMA_CON: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_GDMA_CNT),
		mtk_dma_read_32(i2c, OFFSET_GDMA_CON));
	I2CERR("GDMA_START: 0x%x, GDMA_INTSTA: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_GDMA_START),
		mtk_dma_read_32(i2c, OFFSET_GDMA_INTSTA));
	I2CERR("GDMA_ACKINT: 0x%x, GDMA_RLCT: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_GDMA_ACKINT),
		mtk_dma_read_32(i2c, OFFSET_GDMA_RLCT));
	I2CERR("GDMA_LIMIT: 0x%x\n",
		mtk_dma_read_32(i2c, OFFSET_GDMA_LIMIT));
}

static void i2c_dump_register(struct mtk_i2c *i2c)
{
	unsigned int slv_addr = OFFSET_SLAVE_ADDR;
	struct i2c_driver_quirks *i2c_quirks = &i2c->i2c_quirks[i2c->id];

	if (i2c_quirks->slv_addr_v1)
		slv_addr = OFFSET_SLAVE_ADDR_V1;

	I2CERR("id = %d, Base-address: 0x%x\n", i2c->id, i2c->i2c_multi_base);
	I2CERR("SLAVE_ADDR: 0x%x, INTR_MASK: 0x%x\n",
		mtk_i2c_read_16(i2c, slv_addr),
		mtk_i2c_read_16(i2c, OFFSET_INTR_MASK));
	I2CERR("INTR_STAT: 0x%x, CONTROL: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_INTR_STAT),
		mtk_i2c_read_16(i2c, OFFSET_CONTROL));
	I2CERR("TRANSFER_LEN: 0x%x, TRANSAC_LEN: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_TRANSFER_LEN),
		mtk_i2c_read_16(i2c, OFFSET_TRANSAC_LEN));
	I2CERR("DELAY_LEN: 0x%x, HTIMING: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_DELAY_LEN),
		mtk_i2c_read_16(i2c, OFFSET_TIMING));
	I2CERR("START: 0x%x, EXT_CONF: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_START),
		mtk_i2c_read_16(i2c, OFFSET_EXT_CONF));
	I2CERR("HS: 0x%x, IO_CONFIG: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_HS),
		mtk_i2c_read_16(i2c, OFFSET_IO_CONFIG));
	I2CERR("TRANSFER_LEN_AUX: 0x%x, DEBUGFSM_DEBUG: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_TRANSFER_LEN_AUX),
		mtk_i2c_read_16(i2c, OFFSET_DEBUGFSM));
	I2CERR("CLOCK_DIV: 0x%x, FIFO_STAT: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_CLOCK_DIV),
		mtk_i2c_read_16(i2c, OFFSET_FIFO_STAT));
	I2CERR("DEBUGCTRL : 0x%x, DEBUGSTAT: 0x%x\n",
		mtk_i2c_read_16(i2c, OFFSET_DEBUGCTRL),
		mtk_i2c_read_16(i2c, OFFSET_DEBUGSTAT));
	// I2CERR("LTIMING: 0x%x, MULTI_DMA: 0x%x\n",
	// 	mtk_i2c_read_16(i2c, OFFSET_LTIMING),
	// 	_read_16(i2c->i2c_chn_base + OFFSET_MULTI_DMA));

	if (i2c->dma_en) {
		if (i2c_quirks->dma_type == APDMA_TYPE)
			mtk_apdma_dump(i2c);
		else if (i2c_quirks->dma_type == GDMA_TYPE)
			mtk_gdma_dump(i2c);
	}
}

/* 1. malloc memory buffer
 * 2. Flush/clear cache
 * 3. support 4G bits memory
 */
static int mtk_set_apdma(struct mtk_i2c *i2c, struct i2c_msg *msgs)
{
	int dma_sync = 0;
	int ret = I2C_OK;
	struct i2c_driver_quirks *i2c_quirks = &i2c->i2c_quirks[i2c->id];

	mtk_i2c_write_16(i2c, 0x00, OFFSET_DEBUGCTRL);
	mtk_dma_write_32(i2c, I2C_DMA_HANDSHAKE_RST | I2C_DMA_WARM_RST, OFFSET_RST);
	_udelay(50);
	mtk_dma_write_32(i2c, 0x00, OFFSET_RST);
	mtk_i2c_write_16(i2c, I2C_HANDSHAKE_RST, OFFSET_SOFTRESET);
	mtk_i2c_write_16(i2c, 0x00, OFFSET_SOFTRESET);
	mtk_i2c_write_16(i2c, I2C_RELIABILITY | I2C_DMAACK_ENABLE, OFFSET_DEBUGCTRL);

	if (i2c_quirks->apdma_sync) {
		dma_sync = I2C_DMA_SKIP_CONFIG | I2C_DMA_ASYNC_MODE;
		if (i2c->op == I2C_MASTER_WRRD)
			dma_sync |= I2C_DMA_DIR_CHANGE;
	}

	switch (i2c->op) {
	case I2C_MASTER_WR:
		zx_vmo_op_range(vmo[i2c->id].input_vmo, ZX_VMO_OP_CACHE_CLEAN, 0, vmo[i2c->id].size, NULL, 0);
		// i2c->dma_wr_buf = _malloc_memory(msgs->len, i2c->id, tx_en, rx_en);
		// memcpy(i2c->dma_wr_buf, msgs->buf, msgs->len);
		// _cache_flush((void *)i2c->dma_wr_buf, msgs->len);
		i2c->dma_wr_buf = msgs->buf;
		dma_sync |= I2C_DMA_CON_TX;
		mtk_dma_write_32(i2c, dma_sync, OFFSET_CON);

		// mtk_dma_write_32(i2c, (unsigned int)_memory_map(i2c->id, tx_en, rx_en), OFFSET_TX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)msgs->vbuf, OFFSET_TX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)msgs->len, OFFSET_TX_LEN);
		mtk_dma_write_32(i2c, I2C_DMA_START_EN, OFFSET_EN);
		break;
	case I2C_MASTER_RD:
		zx_vmo_op_range(vmo[i2c->id].output_vmo, ZX_VMO_OP_CACHE_CLEAN, 0, vmo[i2c->id].size, NULL, 0);
		// i2c->dma_rd_buf = _malloc_memory(msgs->len, i2c->id, tx_en, rx_en);
		// memset(i2c->dma_rd_buf, 0, msgs->len);
		i2c->dma_rd_buf = msgs->buf;

		dma_sync |= I2C_DMA_CON_RX;
		mtk_dma_write_32(i2c, dma_sync, OFFSET_CON);
		// mtk_dma_write_32(i2c, (unsigned int)_memory_map(i2c->id, tx_en, rx_en), OFFSET_RX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)msgs->vbuf, OFFSET_RX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)msgs->len, OFFSET_RX_LEN);
		mtk_dma_write_32(i2c, I2C_DMA_START_EN, OFFSET_EN);
		zx_vmo_op_range(vmo[i2c->id].output_vmo, ZX_VMO_OP_CACHE_INVALIDATE, 0, vmo[i2c->id].size, NULL, 0);
		break;
	case I2C_MASTER_WRRD:
		zx_vmo_op_range(vmo[i2c->id].input_vmo, ZX_VMO_OP_CACHE_CLEAN, 0, vmo[i2c->id].size, NULL, 0);
		zx_vmo_op_range(vmo[i2c->id].output_vmo, ZX_VMO_OP_CACHE_CLEAN, 0, vmo[i2c->id].size, NULL, 0);
		// i2c->dma_wr_buf = _malloc_memory(msgs->len, i2c->id, tx_en, rx_en);
		i2c->dma_wr_buf = msgs->buf;
		i2c->dma_rd_buf = (msgs + 1)->buf;

		// i2c->dma_rd_buf = _malloc_memory((msgs + 1)->len, i2c->id, rx_en, rx_en);
		// memcpy(i2c->dma_wr_buf, msgs->buf, msgs->len);
		// memset(i2c->dma_rd_buf, 0, (msgs + 1)->len);
		// _cache_flush((void *)i2c->dma_wr_buf, msgs->len);
		mtk_dma_write_32(i2c, dma_sync, OFFSET_CON);
		// mtk_dma_write_32(i2c, (unsigned int)_memory_map(i2c->id, tx_en, rx_en), OFFSET_TX_MEM_ADDR);
		// mtk_dma_write_32(i2c, (unsigned int)_memory_map(i2c->id, tx_en, rx_en), OFFSET_RX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)msgs->vbuf, OFFSET_TX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)(msgs + 1)->vbuf, OFFSET_RX_MEM_ADDR);
		mtk_dma_write_32(i2c, (unsigned int)msgs->len, OFFSET_TX_LEN);
		mtk_dma_write_32(i2c, (unsigned int)(msgs + 1)->len, OFFSET_RX_LEN);
		mtk_dma_write_32(i2c, I2C_DMA_START_EN, OFFSET_EN);
		// _cache_invalid(i2c->dma_rd_buf, (msgs + 1)->len);
		zx_vmo_op_range(vmo[i2c->id].output_vmo, ZX_VMO_OP_CACHE_INVALIDATE, 0, vmo[i2c->id].size, NULL, 0);
		break;
	default:
		I2CERR("APDMA not support current operation mode(i2c->op=%d)\n", i2c->op);
		ret = -EINVAL_I2C;
	}
	return ret;
}

static int mtk_set_gdma(struct mtk_i2c *i2c, struct i2c_msg *msgs)
{
	int ret = I2C_OK;

	switch (i2c->op) {
	case I2C_MASTER_WR:
		mtk_dma_write_32(i2c, (unsigned long)msgs->buf, OFFSET_GDMA_SRC);
		mtk_dma_write_32(i2c, (unsigned int)(i2c->i2c_multi_base + OFFSET_DATA_PORT), OFFSET_GDMA_DST);
		mtk_dma_write_32(i2c, msgs->len, OFFSET_GDMA_CNT);
		mtk_dma_write_32(i2c, GDMA_DREQ | GDMA_SINC, OFFSET_GDMA_CON);
		mtk_dma_write_32(i2c, GDMA_START, OFFSET_GDMA_START);
		break;
	case I2C_MASTER_RD:
		mtk_dma_write_32(i2c, (unsigned long)(i2c->i2c_multi_base + OFFSET_DATA_PORT), OFFSET_GDMA_SRC);
		mtk_dma_write_32(i2c, (unsigned long)msgs->buf, OFFSET_GDMA_DST);
		mtk_dma_write_32(i2c, msgs->len, OFFSET_GDMA_CNT);
		mtk_dma_write_32(i2c, GDMA_DREQ | GDMA_DINC, OFFSET_GDMA_CON);
		mtk_dma_write_32(i2c, GDMA_START, OFFSET_GDMA_START);
		break;
	default:
		I2CERR("GDMA not support current operation mode(i2c->op=%d)\n", i2c->op);
		ret = -EINVAL_I2C;
	}

	return ret;
}

static int mtk_config_dma_mode(struct mtk_i2c *i2c, struct i2c_msg *msgs)
{
	int ret = -EINVAL_I2C;
	struct i2c_driver_quirks *i2c_quirks = &i2c->i2c_quirks[i2c->id];

	if (!i2c->pdmabase) {
		I2CERR("Dma base address is NULL!\n");
		goto err_exit;
	}

	if (i2c_quirks->dma_type == APDMA_TYPE)
		ret = mtk_set_apdma(i2c, msgs);
	else if (i2c_quirks->dma_type == GDMA_TYPE)
		ret = mtk_set_gdma(i2c, msgs);
	else
		I2CERR("The driver doesn't support APDMA/GDMA mode\n");

err_exit:
	return ret;
}

static void mtk_config_fifo_mode(struct mtk_i2c *i2c, struct i2c_msg *msgs)
{
	unsigned int data_size = msgs->len;
	unsigned char *ptr = msgs->buf;

	if (i2c->op != I2C_MASTER_RD) {
		while (data_size--) {
			mtk_i2c_write_8(i2c, *ptr, OFFSET_DATA_PORT);
			ptr++;
		}
	}
}

static int mtk_i2c_param_check(struct mtk_i2c *i2c, struct i2c_msg *msgs, int num)
{
	int i = 0;
	int ret = I2C_OK;
	struct i2c_driver_quirks *i2c_quirks = &i2c->i2c_quirks[i2c->id];

	if (num == 0 || !i2c_quirks->channel_en) {
		I2CERR("i2c_quirks->channel_en =%d\n", i2c_quirks->channel_en);
		ret = -EINVAL_I2C;
	}

	for (i = 0; i < num; i++) {
		if (msgs[i].addr == 0x00 || msgs[i].addr >= 0x80 || msgs[i].buf == NULL) {
			I2CERR("Invalid device address or buffer is NULL\n");
			ret = -EINVAL_I2C;
			goto err_exit;
		}

		if (msgs[i].len > MAX_FIFO_LEN) {
			if (i2c_quirks->dma_type != NONE_DMA) {
				i2c->dma_en = true;
			} else {
				I2CERR("Only support fifo mode, msgs length must be less than 16 bytes\n");
				ret = -EINVAL_I2C;
				goto err_exit;
			}
		}
	}

err_exit:
	return ret;
}

static int mtk_i2c_do_transfer(struct mtk_i2c *i2c, struct i2c_msg *msgs,
			       int num, int left_num)
{
	unsigned short addr_reg;
	unsigned short start_reg;
	unsigned short control_reg;
	unsigned short restart_flag = 0;
	unsigned int data_size = msgs->len;
	unsigned char *ptr = msgs->buf;
	unsigned int tmo_poll = TIMEOUT;
	struct i2c_driver_quirks *i2c_quirks = &i2c->i2c_quirks[i2c->id];
	int ret = I2C_OK;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	control_reg = mtk_i2c_read_16(i2c, OFFSET_CONTROL) &
			~(I2C_CONTROL_DIR_CHANGE | I2C_CONTROL_RS |
			I2C_CONTROL_DMA_EN);

	if ((i2c->speed > I2C_MAX_FAST_MODE_PLUS_FREQ) || (left_num >= 1))
		control_reg |= I2C_CONTROL_RS;

	if (i2c->op == I2C_MASTER_WRRD)
		control_reg |= I2C_CONTROL_DIR_CHANGE | I2C_CONTROL_RS;

	if (i2c->dma_en)
		control_reg |= I2C_CONTROL_DMA_EN;

	mtk_i2c_write_16(i2c, control_reg, OFFSET_CONTROL);

	/*Set slave address*/
	addr_reg = (i2c->op == I2C_MASTER_RD) ? ((msgs->addr << 1) | 0x1) :
		   (msgs->addr << 1);

	if (i2c_quirks->slv_addr_v1)
		mtk_i2c_write_16(i2c, addr_reg, OFFSET_SLAVE_ADDR_V1);
	else
		mtk_i2c_write_16(i2c, addr_reg, OFFSET_SLAVE_ADDR);

	/* Clear interrupt status */
	mtk_i2c_write_16(i2c, restart_flag | I2C_CONFERR | I2C_HS_NACKERR | I2C_ACKERR |
			    I2C_ARB_LOST | I2C_TRANSAC_COMP, OFFSET_INTR_STAT);

	if (i2c_quirks->multi_chn != I2C_CHN)
		mtk_i2c_write_16(i2c, I2C_FIFO_ADDR_CLR_MCH, OFFSET_FIFO_ADDR_CLR);
	else
		mtk_i2c_write_16(i2c, I2C_FIFO_ADDR_CLR, OFFSET_FIFO_ADDR_CLR);

	if (!i2c->dma_en)
		i2c_quirks->poll_en = true;
	else
		i2c_quirks->poll_en = false;

	/* Enable interrupt */
	if (!i2c_quirks->poll_en)
		mtk_i2c_write_16(i2c, restart_flag | I2C_CONFERR | I2C_HS_NACKERR | I2C_ACKERR |
			    I2C_ARB_LOST | I2C_TRANSAC_COMP, OFFSET_INTR_MASK);
	else
		mtk_i2c_write_16(i2c, I2C_INTR_MASK_ALL, OFFSET_INTR_MASK);

	/* Set transfer and transaction len */
	if (i2c->op == I2C_MASTER_WRRD) {
		mtk_i2c_write_16(i2c, msgs->len, OFFSET_TRANSFER_LEN);
		mtk_i2c_write_16(i2c, (msgs + 1)->len, OFFSET_TRANSFER_LEN_AUX);
		mtk_i2c_write_16(i2c, I2C_WRRD_TRANAC_VALUE, OFFSET_TRANSAC_LEN);
	} else {
		mtk_i2c_write_16(i2c, msgs->len, OFFSET_TRANSFER_LEN);
		mtk_i2c_write_16(i2c, num, OFFSET_TRANSAC_LEN);
	}

	if (i2c->dma_en)
		mtk_config_dma_mode(i2c, msgs);
	else
		mtk_config_fifo_mode(i2c, msgs);

	if (!i2c->auto_restart) {
		start_reg = I2C_TRANSAC_START;
	} else {
		start_reg = I2C_TRANSAC_START | I2C_RS_MUL_TRIG;
		if (left_num >= 1)
			start_reg |= I2C_RS_MUL_CNFG;
	}

	mtk_i2c_write_16(i2c, start_reg, OFFSET_START);

	if (i2c_quirks->poll_en) {
		while (1) {
			/*check the interrupt status register*/
			i2c->irq_stat = mtk_i2c_read_16(i2c, OFFSET_INTR_STAT);
			if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR)) {
				ret = -EACK_ERR_I2C;
				break;
			} else if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag)) {
				ret = I2C_OK;
				break;
			}
			_udelay(1);
			tmo_poll--;
			if (tmo_poll == 0) {
				ret = -ETIMEDOUT_I2C;
				break;
			}
		}
		/* Clear Interrupt status*/
		mtk_i2c_write_16(i2c, i2c->irq_stat, OFFSET_INTR_STAT);
	} else {
		ret = _wait_irq_complete(i2c->id);
		if (ret != I2C_OK) {
			i2c_dump_register(i2c);
			mtk_i2c_init_hw(i2c);
			if (i2c_quirks->multi_chn != I2C_CHN && ret != I2C_OK) {
				mtk_i2c_write_16(i2c, 0x3f, OFFSET_SOFTRESET);
				_udelay(5);
				mtk_i2c_write_16(i2c, 0x00, OFFSET_SOFTRESET);
			}
			return ret;
		}
	}

	/* Clear interrupt mask */
	mtk_i2c_write_16(i2c, ~(restart_flag | I2C_CONFERR | I2C_HS_NACKERR | I2C_ACKERR |
			    I2C_ARB_LOST | I2C_TRANSAC_COMP), OFFSET_INTR_MASK);

	if (ret == I2C_OK && i2c->op != I2C_MASTER_WR) {
		ptr = (i2c->op == I2C_MASTER_RD) ? msgs->buf : (msgs + 1)->buf;
		if (!i2c->dma_en) {
			/* transfer success ,we need to get data from fifo */
			data_size = (mtk_i2c_read_16(i2c, OFFSET_FIFO_STAT) & 0xF);
			while (data_size--) {
				*ptr = mtk_i2c_read_8(i2c, OFFSET_DATA_PORT);
				ptr++;
			}
		}
	}

	if (ret == -ETIMEDOUT_I2C) {
		I2CERR("addr: %x, transfer timeout\n", msgs->addr);
		i2c_dump_register(i2c);
		mtk_i2c_init_hw(i2c);
		mtk_i2c_write_16(i2c, 0x3f, OFFSET_SOFTRESET);
		_udelay(10);
		mtk_i2c_write_16(i2c, 0x00, OFFSET_SOFTRESET);
	}

	if (ret == -EACK_ERR_I2C) {
		I2CERR("addr: %x, transfer ACK error\n", msgs->addr);
		i2c_dump_register(i2c);
		mtk_i2c_init_hw(i2c);
	}

	return ret;
}

static int mtk_i2c_transfer(struct mtk_i2c *i2c, struct i2c_msg msgs[], int num)
{
	int ret;
	int left_num = num;
	struct i2c_driver_quirks *i2c_quirk = NULL;

	i2c->i2c_quirks = mtk_i2c_driver_quirks;

	ret = mtk_i2c_param_check(i2c, msgs, num);
	if (ret < 0)
		goto err_exit;

	i2c_quirk = &i2c->i2c_quirks[i2c->id];


	ret = mtk_i2c_set_speed(i2c);
	if (ret < 0)
		goto err_exit;

	mtk_i2c_init_hw(i2c);

	/* checking if we can skip restart and optimize using WRRD mode */
	if ((num == 1) || (num == 2 && (!(msgs[0].flags & I2C_M_RD) &&
		(msgs[1].flags & I2C_M_RD) && msgs[0].addr == msgs[1].addr))) {
		i2c->auto_restart = false;
	} else {
		i2c->auto_restart = true;
	}

	while (left_num--) {
		if (msgs->flags & I2C_M_RD)
			i2c->op = I2C_MASTER_RD;
		else
			i2c->op = I2C_MASTER_WR;

		if (!i2c->auto_restart) {
			if (num == 2 && (i2c->dma_en == false ||
				i2c_quirk->dma_type == APDMA_TYPE)) {
				/* combined two messages into one transaction
				 * since GDMA does not support WRRD mode, use
				 * multi-transfer instead of WRRD.
				 */
				i2c->op = I2C_MASTER_WRRD;
				left_num--;
			}
		}
		ret = mtk_i2c_do_transfer(i2c, msgs, num, left_num);
		if (ret < 0)
			goto err_exit;

		msgs++;
	}

	err_exit:
	return ret;
}

/** @ingroup IP_group_i2c_external_function
* @par Description
*     Read data from slave device.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
*              Since adsp only uses I2C2, the id needs to be set to 2.
*     device_addr: The slave device 7-bits address.
*     speed_khz: Setting i2c transfer speed,example 100, 400, 1000.
*     buffer: Read data buffer.
*     len: Read data length.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_read(unsigned char bus_num, unsigned char device_addr,
	unsigned int speed_khz, unsigned char *buffer, unsigned char *vbuffer, unsigned int len)
{
	struct i2c_msg msgs;
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_num);
	int ret = 0;

	ret = _lock_i2c_bus(bus_num);
	if (ret) {
		I2CERR("I2C%d bus is busy\n", bus_num);
		return ret;
	}

	i2c->id = bus_num;
	i2c->speed = speed_khz;
	i2c->use_push_pull = false;
	i2c->dma_en = false;
	i2c->msg_complete = false;
	i2c->irq_stat = 0;
	i2c->dma_rd_buf = NULL;
	i2c->dma_wr_buf = NULL;

	msgs.addr = device_addr;
	msgs.flags = 1;
	msgs.buf = buffer;
	msgs.vbuf = vbuffer;
	msgs.len = len;

	ret = mtk_i2c_transfer(i2c, &msgs, 1);

	_unlock_i2c_bus(bus_num);

	return ret;
}

/** @ingroup IP_group_i2c_external_function
* @par Description
*     Write data to slave device.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
*              Since adsp only uses I2C2, the id needs to be set to 2.
*     device_addr: The slave device 7-bits address.
*     speed_khz: Setting i2c transfer speed,example 100, 400, 1000.
*     buffer: write data buffer, Contains the slave device register address and
*             the data to be set.
*     len: Write data length.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_write_only(unsigned char bus_num, unsigned char device_addr,
	unsigned int speed_khz, unsigned char *buffer, unsigned int len, void *i2c_addr)
{
	struct i2c_msg msgs;
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_num);
	int ret = 0;
	
	i2c->i2c_multi_base = i2c_addr;

	ret = _lock_i2c_bus(bus_num);
	if (ret) {
		I2CERR("I2C%d bus is busy\n", bus_num);
		return ret;
	}

	i2c->id = bus_num;
	i2c->speed = speed_khz;
	i2c->use_push_pull = false;
	i2c->dma_en = false;
	i2c->msg_complete = false;
	i2c->irq_stat = 0;
	i2c->dma_rd_buf = NULL;
	i2c->dma_wr_buf = NULL;

	msgs.addr = device_addr;
	msgs.flags = 0;
	msgs.buf = buffer;
	msgs.len = len;

	ret = mtk_i2c_transfer(i2c, &msgs, 1);

	_unlock_i2c_bus(bus_num);

	return ret;
}


int mtk_i2c_write(unsigned char bus_num, unsigned char device_addr,
	unsigned int speed_khz, unsigned char *buffer, unsigned char *vbuffer,unsigned int len)
{
	struct i2c_msg msgs;
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_num);
	int ret = 0;

	i2c->i2c_multi_base = bus[bus_num].base;
	i2c->pdmabase = bus[bus_num].pdma_base;

	ret = _lock_i2c_bus(bus_num);
	if (ret) {
		I2CERR("I2C%d bus is busy\n", bus_num);
		return ret;
	}

	i2c->id = bus_num;
	i2c->speed = speed_khz;
	i2c->use_push_pull = false;
	i2c->dma_en = false;
	i2c->msg_complete = false;
	i2c->irq_stat = 0;
	i2c->dma_rd_buf = NULL;
	i2c->dma_wr_buf = NULL;

	msgs.addr = device_addr;
	msgs.flags = 0;
	msgs.buf = buffer;
	msgs.vbuf = vbuffer;
	msgs.len = len;

	ret = mtk_i2c_transfer(i2c, &msgs, 1);

	_unlock_i2c_bus(bus_num);

	return ret;
}

/** @ingroup IP_group_i2c_external_function
* @par Description
*     Read the value of the specified register from the device.
* @param[in]
*     bus_num: I2C bus id where the slave device to be accessed is located.
*     Since adsp only uses I2C2, the id needs to be set to 2.
*     device_addr: The slave device 7-bits address.
*     speed_khz: Setting i2c transfer speed,example 100, 400, 1000.
*     write_buffer: The slave device register address,that is, the register location
*     of the slave device to be read.
*     read_buffer: Read data buffer.
*     write_len: Write data length.
*     read_len: Read data length.
* @return
*     0: i2c transmission successful.
*     Negative value: i2c transmission failed.
* @par Boundary case and Limitation
*     bus_num only support I2C2 due to adsp only user i2c2.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_write_read(unsigned char bus_num, unsigned char device_addr,
	unsigned int speed_khz, unsigned char *write_buffer, unsigned char *write_vbuffer,
	unsigned char *read_buffer, unsigned char *read_vbuffer, unsigned int write_len, unsigned int read_len)
{
	struct i2c_msg msgs[2];
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_num);

	int ret = 0;

	ret = _lock_i2c_bus(bus_num);
	if (ret) {
		I2CERR("I2C%d bus is busy\n", bus_num);
		return ret;
	}

	i2c->id = bus_num;
	i2c->speed = speed_khz;
	i2c->use_push_pull = false;
	i2c->dma_en = false;
	i2c->msg_complete = false;
	i2c->irq_stat = 0;
	i2c->dma_rd_buf = NULL;
	i2c->dma_wr_buf = NULL;

	msgs[0].addr = device_addr;
	msgs[0].flags = 0;
	msgs[0].buf = write_buffer;
	msgs[0].vbuf = write_vbuffer;
	msgs[0].len = write_len;

	msgs[1].addr = device_addr;
	msgs[1].flags = 1;
	msgs[1].buf = read_buffer;
	msgs[1].vbuf = read_vbuffer;
	msgs[1].len = read_len;

	ret = mtk_i2c_transfer(i2c, msgs, 2);

	_unlock_i2c_bus(bus_num);

	return ret;
}

/* 1. config i2c pinmux confg
 * 2. Enable I2C multi-user function
 * E.g. MT8519 I2C channel application
 * I2C0: AP & RV33;
 * I2C1: AP & MCUPM;
 * I2C2: AP & ADSP
 * 3. Config mcu interrupt.
 */

/** @ingroup IP_group_i2c_external_function
* @par Description
*     mtk_i2c_hw_init(): Initialize i2c hardware related setting.
*     It only needs to be called once.usually already called in main.c
* @param[in]
*     none.
* @return
*     none.
* @par Boundary case and Limitation
*     none.
* @par Error case and Error handling
*     none.
* @par Refer to the source code
*/
int mtk_i2c_hw_init(void)
{
	struct mtk_i2c *i2c = NULL;
	unsigned char bus_id = 0;
	unsigned char cnt = 0;
	unsigned char max_chn_id = 0;
	unsigned short reg_val = 0;
	struct i2c_driver_quirks *i2c_quirk = NULL;
	int ret = -EINVAL_I2C;

	for (bus_id = 0; bus_id < MTK_I2C_MAX_PORT_NUMBER; bus_id++) {
		i2c = mtk_i2c_get_bus(bus_id);
		if (i2c == NULL)
			goto err_exit;

		mtk_i2c_base_init(i2c);

		i2c_quirk = &i2c->i2c_quirks[bus_id];
		max_chn_id = (unsigned char)i2c_quirk->max_chn_id;

		if (i2c_quirk->channel_en == false)
			continue;

		ret = _i2c_gpio_set(bus_id);
		if (ret < 0)
			goto err_exit;

		// if (!i2c_quirk->poll_en) {
		// 	ret = _request_irq(i2c->id);
		// 	if (ret < 0)
		// 		goto err_exit;
		// }

		_i2c_clk_enable(bus_id);

		reg_val = _read_16(i2c->i2c_chn_base + OFFSET_MULTI_DMA) & I2C_MULTI_USER_EN;
		if (!reg_val && i2c_quirk->multi_chn) {
			_write_16(i2c->i2c_chn_base + OFFSET_MULTI_DMA, I2C_MULTI_USER_EN);

			for (cnt = 0; cnt < max_chn_id; cnt++)
				_write_16(i2c->i2c_chn_base + 0x100 * (cnt + 1) + OFFSET_MCU_INTR, I2CBIT(cnt));
		}

			_i2c_clk_disable(bus_id);
	}

err_exit:
	return ret;
}

