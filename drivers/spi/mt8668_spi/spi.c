// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/pci.h>
#include <hw/reg.h>
#include <threads.h>
#include <assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "spi.h"
#include "sync_write.h"
#include <ddk/io-buffer.h>
#include <time.h>

static struct mtk_spi_bus bus[MAX_SPI_BUS];
zx_handle_t spi_bti;
#define IO_SIZE				0x02000000
#define SPI_CS_HOLD_WARN_NS ZX_MSEC(100)

static void __mtk_spi_hw_set_cs(int bus_id, bool enable);

static void mtk_spi_log_cs_release(spi_device_t* dev, bool forced)
{
	if (!dev->cs_acquired_at) {
		return;
	}

	zx_duration_t held_ns = zx_clock_get(ZX_CLOCK_MONOTONIC) - dev->cs_acquired_at;
	if (held_ns >= SPI_CS_HOLD_WARN_NS) {
		zxlogf(ERROR, "spi: bus %d CS held for %lld us%s\n", dev->bus_id,
			(long long)(held_ns / ZX_USEC(1)), forced ? " before forced release" : "");
	}
	dev->cs_acquired_at = 0;
}

static void mtk_spi_release_cs_locked(spi_device_t* dev, int bus_id, bool forced)
{
	mtk_spi_log_cs_release(dev, forced);
	__mtk_spi_hw_set_cs(bus_id, true);
	dev->cs_owner = NULL;
	dev->cs_active = false;
}

/* ----------------------------------------------------------------------------
 * Lifecycle helpers
 *
 * The SPI driver exposes two device tiers:
 *   parent  : spi_device_t  (per-bus, created in spi_bind, destroyed in spi_release)
 *   child   : spi_client_device_t (per fd open, created in spi_open, destroyed
 *                                  in spi_client_release)
 *
 * Children hold a back-pointer to the parent. Without coordination, when a
 * userspace consumer (e.g. an SOS process serving a guest VM that just rebooted)
 * exits abruptly, devhost may dispatch close()/release() against children
 * AFTER the parent's release() has already freed spi_device_t storage,
 * causing a fatal page fault in spi_client_close() on the very first
 * `client->spi` deref.
 *
 * The helpers below give us:
 *   - O(1) attach/detach of children into spi_device_t::clients[]
 *   - parent refcount that is bumped per live child so the parent's release
 *     waits until every child has released before freeing storage
 *   - tearing_down flag to refuse new opens / new ioctls during teardown
 * ----------------------------------------------------------------------------
 */

/* Caller must NOT hold client_lock. Returns slot index, or -1 if full or
 * the parent is already tearing down (in which case the caller must reject
 * the open). On success, atomically bumps parent refcount. */
static int spi_attach_client_locked(spi_device_t* dev, spi_client_device_t* c)
{
	mtx_lock(&dev->client_lock);
	if (atomic_load(&dev->tearing_down)) {
		mtx_unlock(&dev->client_lock);
		return -1;
	}
	if (dev->client_count >= SPI_MAX_CLIENTS) {
		mtx_unlock(&dev->client_lock);
		return -1;
	}
	int slot = -1;
	for (int i = 0; i < SPI_MAX_CLIENTS; i++) {
		if (dev->clients[i] == NULL) {
			dev->clients[i] = c;
			c->client_slot = i;
			dev->client_count++;
			slot = i;
			break;
		}
	}
	mtx_unlock(&dev->client_lock);
	if (slot >= 0) {
		atomic_fetch_add(&dev->refcount, 1);
	}
	return slot;
}

/* Idempotent. Removes the client from the parent's array if still present.
 * Does NOT drop the parent refcount; the caller (spi_client_release) is
 * responsible for that exactly once. */
static void spi_detach_client(spi_device_t* dev, spi_client_device_t* c)
{
	mtx_lock(&dev->client_lock);
	int slot = c->client_slot;
	if (slot >= 0 && slot < SPI_MAX_CLIENTS && dev->clients[slot] == c) {
		dev->clients[slot] = NULL;
		dev->client_count--;
		c->client_slot = -1;
	}
	mtx_unlock(&dev->client_lock);
}

/* Drop one reference. If this is the last reference, free the parent
 * spi_device_t and any handles owned by it. Safe to call from either the
 * parent's release path or a child's release path; whichever happens last
 * performs the free. */
static void spi_parent_unref(spi_device_t* dev)
{
	if (atomic_fetch_sub(&dev->refcount, 1) != 1) {
		return;
	}

	/*
	 * Last reference. No clients can exist (they would each hold a ref),
	 * and the parent itself has gone through unbind+release.
	 */
	if (dev->regs_handle && dev->regs_handle != (zx_handle_t)-1) {
		zx_handle_close(dev->regs_handle);
		dev->regs_handle = (zx_handle_t)-1;
	}
	if (dev->event) {
		zx_handle_close(dev->event);
		dev->event = 0;
	}
	if (dev->dma_event) {
		zx_handle_close(dev->dma_event);
		dev->dma_event = 0;
	}
	if (dev->irq_handle) {
		zx_handle_close(dev->irq_handle);
		dev->irq_handle = 0;
	}
	free(dev);
}

int mt_set_gpio_mode(enum GPIO_PIN pin, enum GPIO_MODE mode, zx_device_t* dev)
{
	unsigned int pos;
	unsigned int bit;
	unsigned int data;
	unsigned int mask;

	void* gpio_base;
	zx_status_t status;
	io_buffer_t buffer;

	// map resources and initialize the device
	spi_device_t* device = calloc(1, sizeof(spi_device_t));
	if (!device)
		return ZX_ERR_NO_MEMORY;

	if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV,
			(void*) &device->pdev))
		return ZX_ERR_NOT_SUPPORTED;

	status = pdev_get_bti(&device->pdev, 0, &device->bti);
	if (status != ZX_OK) {
		zxlogf(ERROR, "gpio: could not get BTI handle: %d\n", status);
		return -ERINVAL;
	}

	status = io_buffer_init_physical(&buffer, device->bti, GPIO_BASE, IO_SIZE,
								get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
	if (status != ZX_OK) {
		zxlogf(ERROR, "eint: io_buffer_init_physical failed: %d\n", status);
		return status;
	}
	gpio_base = io_buffer_virt(&buffer);

	if ((pin >= MAX_GPIO_PIN) || (pin <= GPIO_UNSUPPORTED))
		return -ERINVAL;

	if (mode >= GPIO_MODE_MAX)
		return -ERINVAL;

#ifdef SUPPORT_MODE_MWR
	mask = (1UL << (GPIO_MODE_BITS - 1)) - 1;
#else
	mask = (1UL << GPIO_MODE_BITS) - 1;
#endif

	mode = mode & mask;
	pos = pin / MAX_GPIO_MODE_PER_REG;
	bit = (pin % MAX_GPIO_MODE_PER_REG) * GPIO_MODE_BITS;

#ifdef SUPPORT_MODE_MWR
	data = (1UL << (bit + GPIO_MODE_BITS - 1)) | (mode << bit);
	uintptr_t addr = (uintptr_t)gpio_base + pos * 0x10 + REGMWR;
	mt_reg_sync_writel(data, addr);
#else
	data = readl(GPIO_MODE_BASE + pos * 0x10);
	data &= (~(mask << bit));
	data |= (mode << bit);
	uintptr_t addr = (uintptr_t)GPIO_MODE_BASE + pos * 0x10;
	mt_reg_sync_writel(data, GPIO_MODE_BASE + pos * 0x10);
#endif

	return RSUCCESS;
}

static void mtk_spi_dump_data(char *name, const char *data, int size)
{
#ifdef DUMP_SPI_DATA
	int i;

	zxlogf(INFO, "%s:\n", name);
	for (i = 0; i < size; i++) {
		if (!(i % 6))
			zxlogf(INFO, "\n");
		zxlogf(INFO, "%x ", data[i]);
	}
	zxlogf(INFO, "\n");
#endif
}

static void mtk_spi_dump_register(int bus_id)
{
	void* base = bus[bus_id].base;
	zxlogf(INFO, "spi_cfg0_reg:0x%x\n", readl(base + SPI_CFG0_REG));
	zxlogf(INFO, "spi_cfg1_reg:0x%x\n", readl(base + SPI_CFG1_REG));
	zxlogf(INFO, "spi_tx_src_reg:0x%x\n", readl(base + SPI_TX_SRC_REG));
	zxlogf(INFO, "spi_rx_dst_reg:0x%x\n", readl(base + SPI_RX_DST_REG));
	zxlogf(INFO, "spi_tx_src_reg:0x%x\n", readl(base + SPI_TX_SRC_REG_64));
	zxlogf(INFO, "spi_rx_dst_reg:0x%x\n", readl(base + SPI_RX_DST_REG_64));
	zxlogf(INFO, "spi_cmd_reg:0x%x\n", readl(base + SPI_CMD_REG));
	zxlogf(INFO, "spi_status1_reg:0x%x\n", readl(base + SPI_STATUS1_REG));
}

static void print_spi_tx_rx_data(zx_paddr_t tx, zx_paddr_t rx, int size)
{
	int print_head = 10;
	int print_tail = 10;
	int print_random = 30;
	const size_t page = 4096;
	io_buffer_t buffer_tx;
	io_buffer_t buffer_rx;
	zx_status_t status;

	zx_paddr_t tx_base = tx & ~(page - 1);
	zx_paddr_t rx_base = rx & ~(page - 1);
	size_t tx_off = (size_t)(tx - tx_base);
	size_t rx_off = (size_t)(rx - rx_base);
	size_t tx_len = (tx_off + size + page - 1) & ~(page - 1);
	size_t rx_len = (rx_off + size + page - 1) & ~(page - 1);

	if (size < 50) {
		print_head = size / 2;
		print_tail = size - print_head;
		print_random = 0;
	}

	status = io_buffer_init_physical(&buffer_tx, spi_bti, tx_base, tx_len,
							get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
	if (status != ZX_OK) {
		zxlogf(ERROR, "tx_buf: io_buffer_init_physical failed: %d\n", status);
	}

	status = io_buffer_init_physical(&buffer_rx, spi_bti, rx_base, rx_len,
							get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
	if (status != ZX_OK) {
		zxlogf(ERROR, "tx_buf: io_buffer_init_physical failed: %d\n", status);
	}

	const uint8_t* tx_ptr = (const uint8_t*)io_buffer_virt(&buffer_tx) + tx_off;
	const uint8_t* rx_ptr = (const uint8_t*)io_buffer_virt(&buffer_rx) + rx_off;

	for (int i = 0; i < print_head; i++) {
		zxlogf(INFO, "DEBUG_SPI:HEAD idx:%d tx:%02x rx:%02x \n", i, tx_ptr[i], rx_ptr[i]);
	}

	srand((unsigned int)time(NULL));
	for (int j = 0; j < print_random; j++) {
		int i = print_head + (rand() % (size - print_head - print_tail));
		zxlogf(INFO, "DEBUG_SPI:MID idx:%d tx:%02x rx:%02x \n", i, tx_ptr[i], rx_ptr[i]);
	}

	for (int i = size - print_tail; i < size; i++) {
		zxlogf(INFO, "DEBUG_SPI:TAIL idx:%d tx:%02x rx:%02x \n", i, tx_ptr[i], rx_ptr[i]);
	}
	zxlogf(INFO, "\n");

	io_buffer_release(&buffer_tx);
	io_buffer_release(&buffer_rx);
}

static void boot_ticks (void) {
	uint64_t freq, tk;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (freq));

	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (tk));

	uint64_t time_s  = tk / freq;
	uint64_t time_ms = (tk * 1000ULL) / freq;
	uint64_t time_us = (tk * 1000000ULL) / freq;

	zxlogf(INFO, "HZ: %lu, tick: %lu, seconds: %lu, ms: %lu, us: %lu\n",
		   freq, tk, time_s, time_ms, time_us);
}

static void mtk_spi_set_gpio_mode(int bus_id, zx_device_t* dev)
{
	const spi_gpio_group_t* group = &spi_gpio_groups[bus_id];
	mt_set_gpio_mode(group->cs, FUN_GPIO, dev);
	mt_set_gpio_mode(group->miso, FUN_SPI, dev);
	mt_set_gpio_mode(group->mosi, FUN_SPI, dev);
	mt_set_gpio_mode(group->clk, FUN_SPI, dev);
}

static void mtk_spi_init(unsigned int speed_hz, int bus_id, zx_device_t* dev)
{
	uint32_t div, sck_ticks, cs_ticks, reg_val;
	void* base = bus[bus_id].base;

	if (speed_hz < (SPI_HZ / 2))
		div = MTK_SPI_ROUNDUP_DIV(SPI_HZ, speed_hz);
	else
		div = 1;

	sck_ticks = MTK_SPI_ROUNDUP_DIV(div, 2);
	cs_ticks = sck_ticks * 2;

	mtk_spi_set_gpio_mode(bus_id, dev);

	/* set the timing */
	mt_reg_sync_writel(((cs_ticks - 1) << SPI_ADJUST_CFG0_CS_HOLD_OFFSET) |
		   ((cs_ticks - 1) << SPI_ADJUST_CFG0_CS_SETUP_OFFSET),
		   base + SPI_CFG0_REG);

	mt_reg_sync_writel(((sck_ticks - 1) << SPI_ADJUST_CFG2_SCK_HIGH_OFFSET) |
		   ((sck_ticks - 1) << SPI_ADJUST_CFG2_SCK_LOW_OFFSET),
		   base + SPI_CFG2_REG);

	reg_val = readl(base + SPI_CFG1_REG);
	reg_val &= ~SPI_CFG1_CS_IDLE_MASK;
	reg_val |= (cs_ticks - 1) << SPI_CFG1_CS_IDLE_SHIFT;
	mt_reg_sync_writel(reg_val, base + SPI_CFG1_REG);

	reg_val = readl(base + SPI_SLICE_EN_REG);

	/* set slice_en */
	reg_val &= ~SPI_RX_SLICE_MASK;
	reg_val &= ~SPI_TX_SLICE_MASK;
	reg_val |= (1<< SPI_RX_SLICE_EN_SHIFT)|(1<< SPI_TX_SLICE_EN_SHIFT);
	mt_reg_sync_writel(reg_val, base + SPI_SLICE_EN_REG);

	reg_val = readl(base + SPI_CMD_REG);

	reg_val &= ~SPI_CMD_CPHA_EN;
	reg_val &= ~SPI_CMD_CPOL_EN;

	/* set the mlsbx and mlsbtx */
	reg_val |= SPI_CMD_TXMSBF_EN;
	reg_val |= SPI_CMD_RXMSBF_EN;

	/* set the tx/rx endian */
	reg_val &= ~SPI_CMD_TX_ENDIAN_EN;
	reg_val &= ~SPI_CMD_RX_ENDIAN_EN;

	/* set finish interrupt always enable */
	reg_val |= SPI_CMD_FINISH_IE_EN;

	/* disable dma mode */
	reg_val &= ~(SPI_CMD_TX_DMA_EN | SPI_CMD_RX_DMA_EN);

	/* set deassert mode */
	reg_val &= ~SPI_CMD_DEASSERT_EN;

	/* set self loop mode */
	// reg_val |= SPI_CMD_SELF_LOOP_EN;

	/* set tick dly */
	reg_val &= ~SPI_CMD_SPIM_TICK_DLY_MASK;
	reg_val |= (0x17) << SPI_CMD_SPIM_TICK_DLY_SHIFT;

	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
}

static void mtk_spi_reset(int bus_id)
{
	int reg_val;
	void* base = bus[bus_id].base;

	reg_val = readl(base + SPI_CMD_REG);
	reg_val |= 1 << SPI_CMD_RST_SHIFT;
	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);

	reg_val = readl(base + SPI_CMD_REG);
	reg_val &= ~(1 << SPI_CMD_RST_SHIFT);
	reg_val &= ~SPI_CMD_CS_PIN_SEL_EN;
	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
}

static void mtk_spi_start(int bus_id)
{
	int reg_val;
	void* base = bus[bus_id].base;
}

static void mtk_spi_stop(int bus_id)
{
	int reg_val;
	void* base = bus[bus_id].base;

	mtk_spi_reset(bus_id);
}

static void mtk_spi_prepare_transfer(spi_client_device_t* spi, int bus_id)
{
	unsigned int div, sck_time, reg_val;
	void* base = bus[bus_id].base;

	if (spi->data->head.speed_hz < spi->data->head.spi_clk_hz / 2)
		div = DIV_ROUND_UP(spi->data->head.spi_clk_hz, spi->data->head.speed_hz);
	else
		div = 1;

	sck_time = (div + 1) / 2;

	unsigned int final_sck = spi->data->head.spi_clk_hz / div;

	reg_val = readl(base + SPI_CMD_REG);
	reg_val |= SPI_CMD_NONIDLE_MODE_EN;
	reg_val |= SPI_CMD_CS_PIN_SEL_EN;
	reg_val &= ~SPI_CMD_DEASSERT_EN;
	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);

	reg_val = readl(base + SPI_CFG2_REG);
	reg_val &= ~(0xffff << SPI_ADJUST_CFG2_SCK_HIGH_OFFSET);
	reg_val |= (((sck_time - 1) & 0xffff) << SPI_ADJUST_CFG2_SCK_HIGH_OFFSET);
	reg_val &= ~(0xffff << SPI_ADJUST_CFG2_SCK_LOW_OFFSET);
	reg_val |= (((sck_time - 1) & 0xffff) << SPI_ADJUST_CFG2_SCK_LOW_OFFSET);
	mt_reg_sync_writel(reg_val, base + SPI_CFG2_REG);
}

static void mtk_spi_packet(int size, int bus_id)
{
	void* base = bus[bus_id].base;
	uint32_t reg_val = 0;
	uint32_t packet_size, packet_size_low, packet_size_high;

	reg_val &= ~(SPI_CFG1_IPM_PACKET_LENGTH_MASK | SPI_CFG1_PACKET_LOOP_MASK);
	packet_size_low = (size - 1) & SPI_PACKET_LENGTH_LOW_MASK;
	packet_size_high = ((size - 1) & SPI_PACKET_LENGTH_HIGH_MASK)
		>> SPI_PACKET_LENGTH_HIGH_OFFSET;
	reg_val |= packet_size_low << SPI_CFG1_PACKET_LENGTH_OFFSET;
	mt_reg_sync_writel(reg_val, base + SPI_CFG1_REG);
	mt_reg_sync_writel(packet_size_high, base + SPI_CFG5_REG);
}

static void mtk_spi_trigger(int bus_id)
{
	int reg_val;
	void* base = bus[bus_id].base;

	reg_val = readl(base + SPI_CMD_REG);
	reg_val |= 1 << SPI_CMD_ACT_SHIFT;
	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
}

static int mtk_spi_transfer_is_end(int bus_id)
{
	int i;
	void* base = bus[bus_id].base;

	i = 0;
	while ((readl(base + SPI_STATUS1_REG) & MTK_SPI_BUSY_STATUS) == 0) {
		i++;
		udelay(1);
		if (i > MTK_TXRX_TIMEOUT_US) {
			zxlogf(INFO, "Timeout waiting for spi status1 register.\n");
			goto error;
		}
	}

	i = 0;
	while ((readl(base + SPI_STATUS0_REG) & MTK_SPI_BUSY_STATUS) == 0) {
		i++;
		udelay(1);
		if (i > MTK_TXRX_TIMEOUT_US) {
			zxlogf(INFO, "Timeout waiting for spi status0 register.\n");
			goto error;
		}
	}

	return 0;

error:
	return -1;
}

static int mtk_spi_fifo_transfer(unsigned char *rx_buf, unsigned char *tx_buf, int size, int bus_id)
{
	int i, reg_val = 0, word_count, ret, remaind;
	void* base = bus[bus_id].base;

	if (!size || size > MTK_FIFO_DEPTH)
		return -1;

	//disable irq
	reg_val = readl(base + SPI_CMD_REG);
	reg_val &= ~(SPI_CMD_FINISH_IE_EN | SPI_CMD_PAUSE_IE_EN);
	writel(reg_val, base + SPI_CMD_REG);

	mtk_spi_packet(size, bus_id);

	remaind = size & 0x03;//size % 4

	if (rx_buf && !tx_buf) {
		word_count = size >> 2;//size / 4

		if (remaind)
			word_count++;

		for (i = 0; i < word_count; i++)
			mt_reg_sync_writel(MTK_ARBITRARY_VALUE, base + SPI_TX_DATA_REG);
	}

	if (tx_buf) {
		reg_val = 0;
		for (i = 0; i < size - remaind; i++) {
			reg_val |= *(tx_buf + i) << ((i & 0x03) << 3);//<< 3 is * 8
			if ((i & 0x03) == 3) {
				mt_reg_sync_writel(reg_val, base + SPI_TX_DATA_REG);
				reg_val = 0;
			}

		}

		if (remaind) {
			reg_val = 0;
			for (i = 0; i < remaind; i++) {
				reg_val |= *(tx_buf + size - remaind + i) << ((i & 0x03) << 3);
			}
			mt_reg_sync_writel(reg_val, base + SPI_TX_DATA_REG);
		}
	}

	mtk_spi_trigger(bus_id);
	ret = mtk_spi_transfer_is_end(bus_id);

	if (!ret && rx_buf) {
		for (i = 0; i < size; i++) {
			if ((i & 0x03) == 0)
				reg_val = readl(base + SPI_RX_DATA_REG);
			*(rx_buf + i) = (reg_val >> ((i & 0x03) << 3)) & 0xff;
		}
		mtk_spi_dump_data("spi after transfer rx_buf data is", rx_buf, size);
	}

	return ret;
}

static int mtk_spi_dma_transfer(spi_client_device_t* spi, int size, int bus_id)
{
	int i, reg_val;
	void* base = bus[bus_id].base;
	uint32_t speed = 1000000;
	zx_signals_t observed;
	zx_status_t status;

	/* If a guest reboot already requested stop, bail out before we program
	 * any guest-supplied physical address into the SPI engine. */
	if (spi->spi->stop_transfer || atomic_load(&spi->spi->tearing_down)) {
		zxlogf(INFO, "spi: stop_transfer set, abort dma_transfer bus=%d\n", bus_id);
		return -1;
	}

	if (spi->data->head.speed_hz)
		speed = spi->data->head.speed_hz;
	uint32_t timeout_ms = 2 * size * 8 * 1000 / speed + 200;

	//clean dma event
	zx_object_signal(spi->spi->dma_event, ZX_USER_SIGNAL_0, 0);

	reg_val = readl(base + SPI_CMD_REG);
	reg_val |= SPI_CMD_FINISH_IE_EN;

	if (spi->data->tx_paddr)
		reg_val |= SPI_CMD_TX_DMA_EN;

	if (spi->data->rx_paddr)
		reg_val |= SPI_CMD_RX_DMA_EN;

	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
	mtk_spi_packet(size, bus_id);

	if (spi->data->tx_paddr) {
		mt_reg_sync_writel((uint32_t)(spi->data->tx_paddr & 0xFFFFFFFFULL), base + SPI_TX_SRC_REG);
		mt_reg_sync_writel((uint32_t)((spi->data->tx_paddr >> 32) & 0xFFFFFFFFULL), base + SPI_TX_SRC_REG_64);
	}

	if (spi->data->rx_paddr) {
		mt_reg_sync_writel((uint32_t)(spi->data->rx_paddr & 0xFFFFFFFFULL), base + SPI_RX_DST_REG);
		mt_reg_sync_writel((uint32_t)((spi->data->rx_paddr >> 32) & 0xFFFFFFFFULL), base + SPI_RX_DST_REG_64);
	}

	mtk_spi_trigger(bus_id);

	status = zx_object_wait_one(spi->spi->dma_event,
											ZX_USER_SIGNAL_0,
											zx_deadline_after(ZX_MSEC(timeout_ms)),
											&observed);
	if (status == ZX_OK && (observed & ZX_USER_SIGNAL_0)) {
		zx_object_signal(spi->spi->dma_event, ZX_USER_SIGNAL_0, 0);
	} else if (status == ZX_ERR_TIMED_OUT) {
		zxlogf(ERROR, "spi wait irq timeout, (observed=0x%x)!\n", observed);
		goto error;
	}

	// if (mtk_spi_transfer_is_end(bus_id))
	// 	goto error;

	reg_val = readl(base + SPI_CMD_REG);
	reg_val &= ~(SPI_CMD_RX_DMA_EN | SPI_CMD_TX_DMA_EN);
	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);

	return 0;

error:
	return -1;
}

int mtk_spi_transfer(spi_client_device_t* spi, int size, int bus_id)
{
	int ret, min_size;

	#ifdef ONLY_SUPPORT_FIFO
		mt_set_gpio_out(CS, OUT_LOW);
	#endif

	mtk_spi_start(bus_id);

	if(size > MTK_PACKET_SIZE ){
		zxlogf(INFO, "spi transfer data size > MTK_PACKET_SIZE \n");
		return 1;
	}

	mtk_spi_prepare_transfer(spi, bus_id);

	while (size) {
		min_size = size < MTK_PACKET_SIZE ? size : MTK_PACKET_SIZE;
		ret = mtk_spi_dma_transfer(spi, min_size, bus_id);

		if (ret) {
			mtk_spi_reset(bus_id);
			return ret;
		}

		size -= min_size;
	}

	mtk_spi_stop(bus_id);

	return ret;
}

static void __mtk_spi_hw_set_cs(int bus_id, bool enable)
{
	uint32_t reg_val;
	void* base = bus[bus_id].base;

	reg_val = readl(base + SPI_CMD_REG);
	if (!enable) {
		reg_val |= SPI_CMD_CS_POL_EN;
		mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
	} else {
		reg_val &= ~SPI_CMD_CS_POL_EN;
		mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
		mtk_spi_reset(bus_id);
	}
}

zx_status_t mtk_spi_set_cs(spi_client_device_t *client,
							int bus_id,
							bool enable)
{
	spi_device_t *dev = client->spi;
	zx_status_t ret = ZX_OK;

	mtx_lock(&dev->cs_lock);

	if (enable) {

		/* A new SET_CS(enable) starts a fresh logical transaction. Clear
		 * any leftover stop_transfer that was set by a guest reboot's
		 * IOCTL_GRT_STOP_SPI_DMA_TRANSFER, otherwise the rebooted SOS would
		 * never be able to issue a transfer again. */
		dev->stop_transfer = false;

		/*already held by os */
		if (dev->cs_active) {

			if (dev->cs_owner == client) {
				/* repeat enable = no-op */
				ret = ZX_OK;
				goto out;
			} else {
				/* Occupied by another OS; backend will retry without blocking this queue */
				ret = ZX_ERR_ALREADY_BOUND;
				goto out;
			}
		}

		/* Acquire CS ownership normally */
		dev->cs_owner  = client;
		dev->cs_active = true;
		dev->cs_acquired_at = zx_clock_get(ZX_CLOCK_MONOTONIC);

		__mtk_spi_hw_set_cs(bus_id, !enable);
	}
	else {
		/* disable */

		if (!dev->cs_active) {
			ret = ZX_ERR_BAD_STATE;
			goto out;
		}

		if (dev->cs_owner != client) {
			ret = ZX_ERR_ACCESS_DENIED;
			goto out;
		}

		/* Release CS */
		mtk_spi_release_cs_locked(dev, bus_id, false);
	}

out:
	mtx_unlock(&dev->cs_lock);
	return ret;
}

// implement device protocol
//
// Order of operations on bus removal:
//   1. spi_unbind(parent ctx)
//        - Mark parent as tearing_down so spi_open() refuses new fds and
//          ioctl paths can short-circuit.
//        - Snapshot the live child list and call device_remove() on each
//          child zx_device_t. Each device_remove triggers devhost to
//          dispatch close() then release() on that child, eventually
//          decrementing parent refcount.
//        - device_remove(parent->zxdev) signals devhost to call our
//          spi_release.
//   2. spi_release(parent ctx)
//        - Drops the parent's own reference. If all children have already
//          finished release, this performs the final free; otherwise the
//          last child to release will free the parent storage.
//
// This avoids the previous fault where a child's close()/release() would
// dereference parent storage that had already been freed when SOS exited.
static void spi_unbind(void* ctx) {
	spi_device_t* spi = ctx;
	zxlogf(INFO, "%s: %d\n", __func__, __LINE__);

	atomic_store(&spi->tearing_down, true);

	/* Force-release CS state at HW level so a stuck transfer doesn't keep
	 * the bus asserted while we tear down. */
	mtx_lock(&spi->cs_lock);
	if (spi->cs_active) {
		mtk_spi_release_cs_locked(spi, spi->bus_id, true);
	}
	mtx_unlock(&spi->cs_lock);

	/*
	 * Snapshot live children under client_lock, then issue device_remove
	 * outside the lock. We must not hold client_lock during device_remove,
	 * because devhost may synchronously call back into spi_client_close /
	 * spi_client_release which take client_lock via spi_detach_client.
	 */
	zx_device_t* to_remove[SPI_MAX_CLIENTS];
	int n_to_remove = 0;

	mtx_lock(&spi->client_lock);
	for (int i = 0; i < SPI_MAX_CLIENTS; i++) {
		spi_client_device_t* c = spi->clients[i];
		if (c && c->device) {
			to_remove[n_to_remove++] = c->device;
		}
	}
	mtx_unlock(&spi->client_lock);

	for (int i = 0; i < n_to_remove; i++) {
		device_remove(to_remove[i]);
	}

	device_remove(spi->zxdev);
}

/*
 * Final stage of parent teardown. After unbind, devhost calls release once
 * the device is fully unhooked from the device tree. We drop the parent's
 * own reference; whichever path drops the last reference (this one or the
 * final child release) is the one that frees the storage and handles.
 *
 * In particular, if a guest reboots and SOS is restarted, but some child
 * release callbacks have not yet been dispatched, this function returns
 * immediately and the children themselves perform the final free safely.
 */
static void spi_release(void* ctx) {
	spi_device_t* spi = ctx;
	atomic_store(&spi->tearing_down, true);
	spi_parent_unref(spi);
}

static zx_status_t spi_client_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
							 void* out_buf, size_t out_len, size_t* out_actual) {
	spi_client_device_t *spi = (spi_client_device_t *)ctx;
	zx_status_t status;

	switch (op) {
		case IOCTL_GRT_SPI_GET_EVENT: {
			if (out_len != sizeof(zx_handle_t)) {
				return ZX_ERR_INVALID_ARGS;
			}
			zx_handle_t* out = (zx_handle_t*) out_buf;
			if ((status = zx_handle_duplicate(spi->spi->event, ZX_RIGHTS_BASIC | ZX_RIGHT_READ, out)) < 0) {
				return status;
			} else {
				*out_actual = sizeof(zx_handle_t);
				return ZX_OK;
			}
		}
		case IOCTL_GRT_SPI_SET_CS: {
			if (in_len != sizeof(grt_spi_cs_t)) {
				return ZX_ERR_INVALID_ARGS;
			}

			grt_spi_cs_t *cs_info = (grt_spi_cs_t *)in_buf;

			zxlogf(INFO, "spi: SET_CS bus %d enable %d\n",
				cs_info->bus, !cs_info->enable);

			return mtk_spi_set_cs(spi, cs_info->bus, !cs_info->enable);
		}
		case IOCTL_GRT_SPI_DMA_TRANSFER: {
			grt_spi_dma_transfer_t *trans = (grt_spi_dma_transfer_t *)in_buf;
			spi_device_t *dev = spi->spi;
			zx_status_t status = ZX_OK;

			spi->data->head.spi_clk_hz = trans->trans_head.spi_clk_hz;
			spi->data->head.bus = trans->trans_head.bus;
			spi->data->head.mode = trans->trans_head.mode;
			spi->data->head.speed_hz = trans->trans_head.speed_hz;
			spi->data->head.word_delay_usecs = trans->trans_head.word_delay_usecs;
			spi->data->head.len = trans->trans_head.len;
			spi->data->head.delay_usecs = trans->trans_head.delay_usecs;
			spi->data->head.chip_select = trans->trans_head.chip_select;
			spi->data->head.bits_per_word = trans->trans_head.bits_per_word;
			spi->data->head.cs_change = trans->trans_head.cs_change;

			if (trans->size < 1 || trans->rx == 0 || trans->tx == 0) {
				return ZX_ERR_INVALID_ARGS;
			}

			mtx_lock(&dev->cs_lock);

			if (!dev->cs_active) {
				status = ZX_ERR_BAD_STATE;
				goto out;
			}

			if (dev->cs_owner != spi) {
				status = ZX_ERR_ACCESS_DENIED;
				goto out;
			}

			spi->data->tx_paddr = trans->tx;
			spi->data->rx_paddr = trans->rx;

			status = mtk_spi_transfer(spi, trans->size, spi->spi->bus_id);

			// print_spi_tx_rx_data(spi->data->tx_paddr, spi->data->rx_paddr, spi->data->head.len);

		out:
			mtx_unlock(&dev->cs_lock);
			return status;
		}
		case IOCTL_GRT_STOP_SPI_DMA_TRANSFER: {
			/*
			 * Invoked from the hypervisor (HC_GUEST_STOP_SPI_DMA) when a
			 * guest OS is rebooting. Any DMA that is still in flight is
			 * pointing at guest physical pages that are about to be
			 * recycled, so we must:
			 *   1. Mark stop_transfer so any new dma_transfer caller
			 *      bails out early.
			 *   2. Disable the DMA engine and clear its src/dst registers
			 *      so the SPI block cannot reach into the dying guest's
			 *      memory.
			 *   3. Reset the SPI block to a known-good state.
			 *   4. Wake any thread waiting on dma_event.
			 *   5. Release CS at HW level and clear cs_active/cs_owner so
			 *      the rebooted guest is not greeted by ZX_ERR_ALREADY_BOUND.
			 *   6. Scrub guest-supplied PAs from the current cs_owner's data
			 *      so a stray follow-on transfer cannot reuse them.
			 */
			spi_device_t *dev = spi->spi;
			void* base = bus[dev->bus_id].base;
			uint32_t reg_val;

			dev->stop_transfer = true;

			mtx_lock(&dev->cs_lock);

			/* (2) disable DMA + clear src/dst */
			reg_val = readl(base + SPI_CMD_REG);
			reg_val &= ~(SPI_CMD_RX_DMA_EN | SPI_CMD_TX_DMA_EN);
			mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
			mt_reg_sync_writel(0, base + SPI_TX_SRC_REG);
			mt_reg_sync_writel(0, base + SPI_TX_SRC_REG_64);
			mt_reg_sync_writel(0, base + SPI_RX_DST_REG);
			mt_reg_sync_writel(0, base + SPI_RX_DST_REG_64);

			/* (3) HW reset */
			mtk_spi_reset(dev->bus_id);

			/* (4) wake any waiter so it observes a non-OK status quickly */
			zx_object_signal(dev->dma_event, 0, ZX_USER_SIGNAL_0);

			/* (6) scrub stale guest PAs from the owner's data, if any */
			if (dev->cs_owner && dev->cs_owner->data) {
				dev->cs_owner->data->tx_paddr = 0;
				dev->cs_owner->data->rx_paddr = 0;
			}

			/* (5) drop CS ownership; raw cs_owner pointer is fine to
			 * compare/clear here because client_lock + spi_detach_client
			 * guarantee a released client never lingers as cs_owner. */
			if (dev->cs_active) {
				mtk_spi_release_cs_locked(dev, dev->bus_id, true);
			}

			mtx_unlock(&dev->cs_lock);

			zxlogf(INFO, "spi: IOCTL_GRT_STOP_SPI_DMA_TRANSFER bus=%d (cleaned)\n",
				dev->bus_id);
			boot_ticks();

			return ZX_OK;
		}

	default:
		return ZX_ERR_NOT_SUPPORTED;
	}
}

/*
 * Close: invoked by devhost when the fd is closed (or when the client zx_device
 * is being removed via device_remove). Must NOT free anything; release() is the
 * single point of teardown. Here we only:
 *   - drop CS ownership if held, so the bus is immediately usable by a new
 *     consumer (e.g. the SOS process restarting after a guest reboot)
 *   - clear stale guest physical addresses to harden subsequent transfers
 *
 * Note: cs_lock and the parent are still valid here because the parent's
 * refcount is held via spi_attach_client_locked().
 */
static zx_status_t spi_client_close(void* ctx, uint32_t flags) {
    spi_client_device_t *client = ctx;
    spi_device_t *dev = client->spi;

    mtx_lock(&dev->cs_lock);
    if (dev->cs_owner == client) {
        zxlogf(INFO, "SPI: force release CS from closing client\n");
        mtk_spi_release_cs_locked(dev, dev->bus_id, true);
    }
    mtx_unlock(&dev->cs_lock);

    /* Clear guest-supplied PAs so an unlikely follow-on transfer cannot
     * reuse a now-stale address. */
    if (client->data) {
        client->data->tx_paddr = 0;
        client->data->rx_paddr = 0;
    }
    return ZX_OK;
}

/*
 * Release: sole owner of teardown. Detaches from the parent, drops the parent
 * refcount, then frees all child storage. After this returns, ctx must not be
 * touched again.
 */
static void spi_client_release(void* ctx) {
    spi_client_device_t *client = ctx;
    spi_device_t *dev = client->spi;

    /* Defensive: cs_owner could in theory still equal client if close() was
     * skipped (devhost only invokes close on explicit fd close, while release
     * is always invoked on device removal). Take the lock and clear it. */
    mtx_lock(&dev->cs_lock);
    if (dev->cs_owner == client) {
        mtk_spi_release_cs_locked(dev, dev->bus_id, true);
    }
    mtx_unlock(&dev->cs_lock);

    spi_detach_client(dev, client);

    free(client->data);
    free(client->spi_cs);   /* previously leaked */
    free(client);

    /* Drop the parent reference taken in spi_open. May trigger the final
     * free of the parent if spi_release has already run. */
    spi_parent_unref(dev);
}

static zx_protocol_device_t client_device_proto = {
	.version = DEVICE_OPS_VERSION,
	.ioctl = spi_client_ioctl,
	.close = spi_client_close,
	.release = spi_client_release,
};

static zx_status_t spi_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
	spi_device_t* parent = ctx;

	/* Refuse new fd opens once the parent is being torn down. Without this,
	 * a late open() racing with spi_unbind() could create a child that
	 * outlives the parent. */
	if (atomic_load(&parent->tearing_down)) {
		return ZX_ERR_BAD_STATE;
	}

	struct spi_client_device* s = calloc(1, sizeof(struct spi_client_device));
	if (!s) {
		return ZX_ERR_NO_MEMORY;
	}
	s->spi = parent;
	s->client_slot = -1;

	grt_spi_data_t *spi_data = calloc(1, sizeof(grt_spi_data_t));
	grt_spi_cs_t   *spi_cs   = calloc(1, sizeof(grt_spi_cs_t));
	if (!spi_data || !spi_cs) {
		free(spi_data);
		free(spi_cs);
		free(s);
		return ZX_ERR_NO_MEMORY;
	}
	s->data   = spi_data;
	s->spi_cs = spi_cs;

	/* Register with the parent BEFORE adding to the device tree, so that
	 * spi_unbind() cannot race past us and free the parent while we are
	 * about to call device_add(). On success, parent refcount is bumped. */
	if (spi_attach_client_locked(parent, s) < 0) {
		zxlogf(ERROR, "spi: spi_open: too many clients or device tearing down\n");
		free(s->data);
		free(s->spi_cs);
		free(s);
		return ZX_ERR_NO_RESOURCES;
	}

	device_add_args_t spi_client_args = {
		.version = DEVICE_ADD_ARGS_VERSION,
		.name = "vm-spi",
		.ctx = s,
		.ops = &client_device_proto,
		.flags = DEVICE_ADD_INSTANCE,
	};

	zx_status_t status = device_add(parent->zxdev, &spi_client_args, &s->device);
	if (status != ZX_OK) {
		/* device_add failed: undo attach and refcount, free everything. */
		spi_detach_client(parent, s);
		free(s->data);
		free(s->spi_cs);
		free(s);
		spi_parent_unref(parent);
		return status;
	}
	*dev_out = s->device;
	return ZX_OK;
}

static zx_protocol_device_t spi_device_proto = {
	.version = DEVICE_OPS_VERSION,
	.unbind = spi_unbind,
	.release = spi_release,
	.open = spi_open,
};

// implement driver object:

void spi_irq_handler(spi_device_t* dev) {
	zx_status_t status;
	uint32_t reg_val;

	reg_val = readl(dev->regs + SPI_STATUS0_REG);

	mtx_lock(&dev->irq_lock);

#ifdef __IRQ_DEBUG__
	if (((dev->irq_count / 100) % 2) == 0) {
		zxlogf(INFO, "ramfb_irq_handler: 0x%x count:%lu\n", curr_addr, dev->irq_count);
	}
	dev->irq_count++;
#endif

	status = zx_object_signal(dev->dma_event, 0, ZX_USER_SIGNAL_0);
	if (status != ZX_OK) {
		zxlogf(ERROR,"zx_object_signal failed: %d\n", status);
	}

	mtx_unlock(&dev->irq_lock);
	return;
}

static zx_status_t spi_irq_thread(void* arg) {
	zx_status_t wait_ret;
	spi_device_t* dev = (spi_device_t*)arg;

	while (true) {
		uint64_t slots;

		wait_ret = zx_interrupt_wait(dev->irq_handle, &slots);
		if (wait_ret != ZX_OK) {
				zxlogf(ERROR, "spi: ======zx_interrupt_wait failed=====>wait_ret:%d\n", wait_ret);
			break;
		}
		// handle irq
		spi_irq_handler(dev);
	}
	return ZX_OK;
}

static zx_status_t spi_irq_init(spi_device_t *dev) {
	zx_status_t status;
	uint32_t reg_val;

	//enable irq
	reg_val = readl(dev->regs + SPI_CMD_REG);
	reg_val |= SPI_CMD_FINISH_IE_EN;
	writel(reg_val, dev->regs + SPI_CMD_REG);

	status = pdev_map_interrupt(&dev->pdev, 0, &dev->irq_handle);
	if (status != ZX_OK) {
		zxlogf(ERROR, "spi: pdev_map_interrupt failed\n");
		return status;
	}

	pdev_get_irq_info(&dev->pdev, 0, &dev->out_irq);
	dev->irq = dev->out_irq.irq;

	status = zx_interrupt_unmask(dev->irq_handle, dev->irq);
	if (status != ZX_OK) {
		zxlogf(ERROR, "spi: failed to unmask irq\n");
		return status;
	}

	status = thrd_create_with_name(&dev->irq_thread, spi_irq_thread, dev,
									"spi-irq");
	if (status != thrd_success) {
		zxlogf(ERROR, "spi: failed to create irq thread\n");
		zx_handle_close(dev->irq_handle);
		return status;
	}
	thrd_detach(dev->irq_thread);

	return ZX_OK;
}

static zx_status_t spi_bind(void* ctx, zx_device_t* dev) {
	zx_status_t status;
	const char* name;
	int id;

	spi_device_t* device = calloc(1, sizeof(spi_device_t));
	if (!device)
		return ZX_ERR_NO_MEMORY;

	if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV,
			(void*) &device->pdev))
		return ZX_ERR_NOT_SUPPORTED;

	// get bus id from name
	name = device_get_name(dev);

	if (sscanf(name, "spi%d", &id) != 1) {
		zxlogf(ERROR, "spi: could not get device id\n");
		return ZX_ERR_NO_MEMORY;
	}

	status = pdev_get_bti(&device->pdev, 0, &device->bti);
	if (status != ZX_OK) {
		zxlogf(ERROR, "spi: could not get BTI handle: %d\n", status);
		goto fail;
	}

	spi_bti = device->bti;

	// map register window
	status = pdev_map_mmio(&device->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
								&device->regs, &device->regs_size,
								&device->regs_handle);
	if (status != ZX_OK) {
		zxlogf(ERROR, "spi: failed to map mmio %d: %d\n", id, status);
		goto fail;
	}

	if ((status = zx_event_create(0, &device->event)) < 0) {
		goto fail;
	}

	//create dma event
	if ((status = zx_event_create(0, &device->dma_event)) < 0) {
		zxlogf(ERROR, "cannot create dma event: %d\n", status);
		goto fail;
	}

	status = spi_irq_init(device);
	if (status != ZX_OK) {
		zxlogf(ERROR, "spi: init interrupt error: %d\n", status);
		goto fail;
	}

	bus[id].base = device->regs;
	bus[id].dev = device;
	device->bus_id = id;
	device->stop_transfer = false;

	mtx_init(&device->lock, mtx_plain);
	mtx_init(&device->cs_lock, mtx_plain);
	mtx_init(&device->irq_lock, mtx_plain);
	mtx_init(&device->client_lock, mtx_plain);

	/* Lifecycle: parent holds a self-reference until spi_release runs. Each
	 * accepted spi_open bumps this; each spi_client_release drops one. */
	atomic_init(&device->refcount, 1);
	atomic_init(&device->tearing_down, false);

	mtk_spi_init(1000000, device->bus_id, dev);

	// create and add the spi (char) device
	device_add_args_t args = {
		.version = DEVICE_ADD_ARGS_VERSION,
		.name = "mtk_spi",
		.ctx = device,
		.ops = &spi_device_proto,
		.flags = DEVICE_ADD_MUST_ISOLATE,
	};

	status = device_add(dev, &args, &device->zxdev);
	if (status != ZX_OK) {
		goto fail;
	}

	return ZX_OK;

fail:
	return status;
}

static zx_driver_ops_t spi_driver_ops = {
	.version = DRIVER_OPS_VERSION,
	.bind = spi_bind,
};

ZIRCON_DRIVER_BEGIN(spi, spi_driver_ops, "zircon", "0.1", 4)
	BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
	BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GRT_MTKV8),
	BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8668),
	BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_SPI),
ZIRCON_DRIVER_END(spi)
