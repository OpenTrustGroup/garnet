/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "i2c_platform.h"
#include "i2c_func.h"

struct i2c_driver_quirks mtk_i2c_driver_quirks[MTK_I2C_MAX_PORT_NUMBER] = {
	[0] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[1] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[2] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[3] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[4] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[5] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[6] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[7] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[8] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[9] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[10] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[11] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},

	[12] = {
		.poll_en = true,
		.apdma_sync = true,
		.slv_addr_v1 = true,
		.channel_en = true,
		.dma_type = APDMA_TYPE,
		.multi_chn = I2C_CHN,
		.max_chn_id = I2C_CH2,
		.src_clk_kHz = SOURCE_CLK_130MHZ,
	},
};

int _plat_i2c_gpio_set(unsigned char bus_id)
{
	int ret = I2C_OK;
#if ENABLE_CONFIG_GPIO
#if (CURRENT_ENV == PL_I2C || CURRENT_ENV == LK_I2C)
	switch (bus_id) {
	case I2C0:
		mt_set_gpio_mode(MTK_GPIO_SDA0, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SDA0, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SDA0, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SDA0, GPIO_RSEL7);

		mt_set_gpio_mode(MTK_GPIO_SCL0, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SCL0, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SCL0, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SCL0, GPIO_RSEL7);
		break;

	case I2C1:
		mt_set_gpio_mode(MTK_GPIO_SDA1, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SDA1, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SDA1, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SDA1, GPIO_RSEL7);

		mt_set_gpio_mode(MTK_GPIO_SCL1, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SCL1, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SCL1, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SCL1, GPIO_RSEL7);
		break;

	case I2C2:
		mt_set_gpio_mode(MTK_GPIO_SDA2, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SDA2, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SDA2, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SDA2, GPIO_RSEL7);

		mt_set_gpio_mode(MTK_GPIO_SCL2, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SCL2, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SCL2, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SCL2, GPIO_RSEL7);
		break;

	case I2C3:
		mt_set_gpio_mode(MTK_GPIO_SDA3, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SDA3, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SDA3, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SDA3, GPIO_RSEL7);

		mt_set_gpio_mode(MTK_GPIO_SCL3, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SCL3, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SCL3, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SCL3, GPIO_RSEL7);
		break;

	case I2C4:
		mt_set_gpio_mode(MTK_GPIO_SDA4, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SDA4, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SDA4, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SDA4, GPIO_RSEL7);

		mt_set_gpio_mode(MTK_GPIO_SCL4, GPIO_MODE_01);
		mt_set_gpio_pull_select(MTK_GPIO_SCL4, GPIO_PULL_UP);
		mt_set_gpio_pull_enable(MTK_GPIO_SCL4, GPIO_PULL_ENABLE);
		mt_set_gpio_rsel(MTK_GPIO_SCL4, GPIO_RSEL7);
		break;

	default:
		ret = -EINVAL_I2C;
		I2CERR("Invaild i2c bus id(%d)\n", bus_id);
		break;
	}
#else
	switch (bus_id) {
	case 0:
		_write_32(0x11D40064, 0x33);  /* set i2c0 Rsel */
		_write_32(0x11D40014, 0x1c7); /* set i2c0 driving */
		break;

	case 1:
		_write_32(0x11D40064, 0xcc);  /* set i2c1 Rsel */
		_write_32(0x11D40014, 0xe38); /* set i2c1 driving */
		break;

	case 2:
		_write_32(0x11F30070, 0xf);  /* set i2c2 Rsel */
		_write_32(0x11F30010, 0xff); /* set i2c2 driving */
		break;

	case 3:
		_write_32(0x11F10090, 0xf);  /* set i2c3 Rsel */
		_write_32(0x11F10020, 0xff); /* set i2c3 driving */
		break;

	case 4:
		_write_32(0x11E20080, 0xf);  /* set i2c4 Rsel */
		_write_32(0x11E20020, 0xff); /* set i2c4 driving */
		break;

	default:
		I2CERR("%s: invalid para: bus_id=%d\n", __func__, bus_id);
		ret = -EINVAL_I2C;
	}
#endif
#endif
	return ret;
}

int _plat_i2c_clock_enable(unsigned char bus_id)
{
	if (bus_id >= MTK_I2C_MAX_PORT_NUMBER)
		return -EINVAL_I2C;

	if (bus_id <= I2C2) {
		_write_32(MTK_I2C_AO_CLK_EN, MTK_I2C_AO_SOURCE_CLK_CLR);
		_write_32(MTK_I2C_AO_CLK_UPDATE_BIT, MTK_I2C_AO_SOURCE_CLK_UPDATE);
		_write_32(EN_AO_BCLK, MTK_AO_BCLK_CG_CLR);
		_write_32(I2CBIT(bus_id), MTK_I2C_AO_CLK_CG_CLR);
	} else {
		_write_32(MTK_I2C_PWR_CLK_EN, MTK_I2C_PWR_SOURCE_CLK_CLR);
		_write_32(MTK_I2C_PWR_CLK_UPDATE_BIT, MTK_I2C_PWR_SOURCE_CLK_UPDATE);
		_write_32(EN_PWR_BCLK, MTK_PWR_BCLK_CG_CLR);
		_write_32(1 << (bus_id - 3), MTK_I2C_PWR_CLK_CG_CLR);
	}

	/*Enable apdma clk cg*/
	if (mtk_i2c_driver_quirks[bus_id].dma_type != NONE_DMA)
		_write_32(MTK_APDMA_CLK_EN, MTK_APDMA_CLK_CLR);

	return 0;
}

void _plat_i2c_clock_disable(unsigned char bus_id)
{
#if !DONT_CTRL_CLK
	if (bus_id >= MTK_I2C_MAX_PORT_NUMBER)
		return;

	/*Disable I2C clk cg*/
	if (bus_id <= I2C2)
		_write_32(I2CBIT(bus_id), MTK_I2C_AO_CLK_CG_SET);
	else
		_write_32(1 << (bus_id - 3), MTK_I2C_PWR_CLK_CG_SET);

	/*Disable apdma clk cg*/
	if (mtk_i2c_driver_quirks[bus_id].dma_type != NONE_DMA)
		_write_32(MTK_APDMA_CLK_DISABLE, MTK_APDMA_CLK_SET);
#endif
}

