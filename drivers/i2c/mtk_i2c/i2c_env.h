/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __I2C_ENV_H__
#define __I2C_ENV_H__

#include "sync_write.h"
#include "../mt8668_i2c/i2c.h"
#include <hw/reg.h>
#include <zircon/types.h>


/*Define DMA type*/
enum mtk_dma_type {
	NONE_DMA = 0,
	APDMA_TYPE,
	GDMA_TYPE,
};

/*Define env type*/
#define PL_I2C 1
#define LK_I2C 2
#define SCP_I2C 3
#define MCUPM_I2C 4
#define APSYS_I2C 5
#define ATF_I2C 6
#define ADSP_I2C 7
#define NEBULA_I2C 8

#define CURRENT_ENV NEBULA_I2C
#define I2C_DEBUG_ENABLE 1

#if CURRENT_ENV == PL_I2C
/*For Preloader*/
#elif CURRENT_ENV == LK_I2C
/*For LK-1.0*/
#include <debug.h>
#include <platform/gpio_api.h>
#include <platform/mt_irq.h>
#include <platform/mtk_timer.h>
#include <reg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if I2C_DEBUG_ENABLE
#define I2CERR(x...) printf(x)
#else
#define I2CERR(x...)
#endif

#elif CURRENT_ENV == SCP_I2C
/*For SCP FreeRTOS*/
#include <string.h>
#include <FreeRTOS.h>
#include <driver_api.h>
#include <mt_printf.h>
#include <task.h>
#include <semphr.h>
#include <interrupt.h>
#include <stdbool.h>
#include <mt_gpt.h>
#include <intc.h>

#if I2C_DEBUG_ENABLE
#define I2CTAG	"[SCP-I2C] "
#define I2CERR(x...)	printf(x)
#else
#define I2CERR(x...)
#endif

#elif CURRENT_ENV == MCUPM_I2C
#include <driver_api.h>
#include <FreeRTOS.h>
#include <irq.h>
#include <mt_printf.h>
#include <mt_remap.h>
#include <ostimer.h>
#include <semphr.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if I2C_DEBUG_ENABLE
#define I2CTAG	"[MCUPM-I2C] "
#define I2CERR(x...)	printf(x)
#else
#define I2CERR(x...)
#endif

#elif CURRENT_ENV == APSYS_I2C
/*For APSYS*/

#elif CURRENT_ENV == ATF_I2C
/*ATF*/
#include <arch_helpers.h>
#include <bl31_fiq_handler.h>
#include <debug.h>
#include <delay_timer.h>
#include <mmio.h>
#include <platform_def.h>
#include <spinlock.h>

#define I2CTAG	"[ATF-I2C] "
#define I2CERR(x...)	ERROR(x)
#elif CURRENT_ENV == ADSP_I2C
/*For HIFI5_ADSP*/
#include <string.h>
#include <FreeRTOS.h>
#include <driver_api.h>
#include <mt_printf.h>
#include <task.h>
#include <semphr.h>
#include <interrupt.h>
#include <stdbool.h>
#include <systimer.h>
#include <mtk_heap.h>
#if I2C_DEBUG_ENABLE
#define I2CTAG	"[ADSP-I2C] "
#define I2CERR(x...)	PRINTF_E(x)
#else
#define I2CERR(x...)
#endif

#ifndef DRV_Reg16
#define  DRV_Reg16(addr) (*(volatile unsigned short *)(addr))
#endif

#ifndef DRV_WriteReg16
#define DRV_WriteReg16(addr, data) ((*(volatile unsigned short *)(addr)) = (unsigned short)(data))
#endif

#define DRAM_MEM_OFFSET 0x10000000
#elif CURRENT_ENV == NEBULA_I2C
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#if I2C_DEBUG_ENABLE
#define I2CERR(x...) printf(x)
#else
#define I2CERR(x...)
#endif

#endif

/*******************************************/

void _write_8(unsigned int addr, unsigned char value);
unsigned char _read_8(unsigned int addr);
void _write_16(unsigned int addr, unsigned short value);
unsigned short _read_16(unsigned int addr);
void _write_32(unsigned int addr, unsigned int value);
unsigned int _read_32(unsigned int addr);
void _udelay(unsigned int us);
void _mdelay(unsigned int us);
int _request_irq(unsigned char bus_id);
int _wait_irq_complete(unsigned char bus_id);
int _i2c_gpio_set(unsigned char bus_id);
int _i2c_clk_enable(unsigned char bus_id);
void _i2c_clk_disable(unsigned char bus_id);
int _lock_i2c_bus(unsigned char bus_id);
void _unlock_i2c_bus(unsigned char bus_id);
void *_malloc_memory(unsigned int size, int bus_num, bool tx_en, bool rx_en);
void _free_memory(int bus_num, bool tx_en, bool rx_en);
unsigned int _memory_map(int bus_num, bool tx_en, bool rx_en);
void _cache_flush(void *pv, unsigned int size);
void _cache_invalid(void *pv, unsigned int size);

#endif
