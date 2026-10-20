/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copyright (c) 2019 MediaTek Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once
#include <assert.h>
#include <endian.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <hw/reg.h>
#include <unistd.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/block.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/block.h>
#include <ddk/phys-iter.h>
#include <pretty/hexdump.h>
//#include <ddk/protocol/ufs.h>
#include <sync/completion.h>
#include <zircon/device/block.h>
#include <zircon/driver/binding.h>
#include <zircon/types.h>
#include <zircon/threads.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define __le32 u32
#define __le16 u16
#define __be32 u32
#define __be16 u16

/*
 * Defined:     Enable full log levels and debugging purpose code.
 * Non-defined: Disable full log levels and debugging purpose code.
 */
/* #define UFS_CFG_DEBUG */

/*
 * Debugging Configuration
 */

//#define UFS_CFG_IO_DEBUG
//#define UFS_CFG_CMD_DEBUG
//#define UFS_CFG_CRYPT_DEBUG
#define UFS_CFG_INIT_DEBUG

#define UFS_LOG_INFO

#if defined(UFS_CFG_DEBUG)
#define UFS_LOG_VERBOSE
#define UFS_LOG_DEBUG
#endif

/* Log level control, lower UFS_DBG_LVL means more log */
#define UFS_LOGF(fmt, ...) \
	zxlogf(ERROR, "[ufs]F:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define UFS_LOGE(fmt, ...) \
	zxlogf(ERROR, "[ufs]E:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)


#define UFS_LOGW(fmt, ...) \
	zxlogf(WARN, "[ufs]W:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

#ifdef UFS_LOG_INFO
#define UFS_LOGI(fmt, ...) \
	zxlogf(INFO, "[ufs]I:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_LOGI(...) do {} while (0)
#endif

#ifdef UFS_LOG_DEBUG
#define UFS_LOGD(fmt, ...) \
	zxlogf(WARN, "[ufs]D:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_LOGD(...) do {} while (0)
#endif

#ifdef UFS_LOG_VERBOSE
#define UFS_LOGT(fmt, ...) \
	zxlogf(TRACE, "[ufs]T:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_LOGT(...) do {} while (0)
#endif

#ifdef UFS_CFG_IO_DEBUG
#define UFS_IO_DBG(fmt, ...) \
	zxlogf(ERROR, "[ufs]IO:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_IO_DBG(...) do {} while (0)
#endif

#ifdef UFS_CFG_CRYPTO_DEBUG
#define UFS_CRYPTO_DBG(fmt, ...) \
	zxlogf(ERROR, "[ufs]CRYPTO:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_CRYPTO_DBG(...) do {} while (0)
#endif

#ifdef UFS_CFG_INIT_DEBUG
#define UFS_INIT_DBG(fmt, ...) \
	zxlogf(ERROR, "[ufs]INIT:%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_INIT_DBG(...) do {} while (0)
#endif

/**
 * upper_32_bits - return bits 32-63 of a number
 * @n: the number we're accessing
 *
 * A basic shift-right of a 64- or 32-bit quantity.  Use this to suppress
 * the "right shift count >= width of type" warning when that quantity is
 * 32-bits.
 */
#ifndef upper_32_bits
#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))
#endif

/**
 * lower_32_bits - return bits 0-31 of a number
 * @n: the number we're accessing
 */
#ifndef lower_32_bits
#define lower_32_bits(n) ((u32)(n))
#endif

/**
 * for sleep and delay (mdelay and udelay)
 */
#ifndef mdelay
#define mdelay(ms) \
    zx_nanosleep(zx_deadline_after(ZX_MSEC(ms)));
#endif

#ifndef udelay
#define udelay(us) \
    zx_nanosleep(zx_deadline_after(ZX_USEC(us)));
#endif

#define UFS_BIT(x)		(1UL << (x))

#define UFS_IRQ_RECEIVED    ZX_EVENT_SIGNALED

#define UFS_ASSERT(x)        ZX_ASSERT(x)

#define min(a,b) ((a) < (b) ? (a) : (b))

#define uintptr_t zx_paddr_t

#ifndef mutex_lock
#define mutex_lock		mtx_lock
#define mutex_unlock	mtx_unlock
#define mutex_t			mtx_t
#endif