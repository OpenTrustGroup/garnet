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
#define IO_SIZE				0x02000000

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
	zxlogf(INFO, "spi_cmd_reg:0x%x\n", readl(base + SPI_CMD_REG));
	zxlogf(INFO, "spi_status1_reg:0x%x\n", readl(base + SPI_STATUS1_REG));
}

static void print_spi_tx_rx_data(const uint8_t* tx, const uint8_t* rx, int size)
{
	int print_head = 10;
	int print_tail = 10;
	int print_random = 30;

	if (size < 50) {
		print_head = size / 2;
		print_tail = size - print_head;
		print_random = 0;
	}

	for (int i = 0; i < print_head; i++) {
		zxlogf(INFO, "DEBUG_SPI:HEAD idx:%d tx:%02x rx:%02x \n", i, tx[i], rx[i]);
	}

	srand((unsigned int)time(NULL));
	for (int j = 0; j < print_random; j++) {
		int i = print_head + (rand() % (size - print_head - print_tail));
		zxlogf(INFO, "DEBUG_SPI:MID idx:%d tx:%02x rx:%02x \n", i, tx[i], rx[i]);
	}

	for (int i = size - print_tail; i < size; i++) {
		zxlogf(INFO, "DEBUG_SPI:TAIL idx:%d tx:%02x rx:%02x \n", i, tx[i], rx[i]);
	}
	zxlogf(INFO, "\n");
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

	if (spi->data->head.speed_hz)
		speed = spi->data->head.speed_hz;
	uint32_t timeout_ms = 2 * size * 8 * 1000 / speed + 200;

	//clean dma event
	zx_object_signal(spi->spi->dma_event, ZX_USER_SIGNAL_0, 0);

	reg_val = readl(base + SPI_CMD_REG);
	reg_val |= SPI_CMD_FINISH_IE_EN;

	if (tx_buf)
		reg_val |= SPI_CMD_TX_DMA_EN;

	if (rx_buf)
		reg_val |= SPI_CMD_RX_DMA_EN;

	mt_reg_sync_writel(reg_val, base + SPI_CMD_REG);
	mtk_spi_packet(size, bus_id);

	if (spi->data->tx_paddr) {
		mt_reg_sync_writel(spi->data->tx_paddr, base + SPI_TX_SRC_REG);
	}

	if (spi->data->rx_paddr) {
		mt_reg_sync_writel(spi->data->rx_paddr, base + SPI_RX_SRC_REG);
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

	if (tx_buf)
		mtk_spi_dump_data("spi before transfer tx_buf data is",tx_buf, size);

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

		/*already held by os */
		if (dev->cs_active) {

			if (dev->cs_owner == client) {
				/* repeat enable = no-op */
				ret = ZX_OK;
			} else {
				/* Occupied by another OS */
				ret = ZX_ERR_ALREADY_BOUND;
			}

			goto out;
		}

		/* Acquire CS ownership normally */
		dev->cs_owner  = client;
		dev->cs_active = true;

		__mtk_spi_hw_set_cs(bus_id, true);
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
		__mtk_spi_hw_set_cs(bus_id, false);

		dev->cs_owner  = NULL;
		dev->cs_active = false;
	}

out:
	mtx_unlock(&dev->cs_lock);
	return ret;
}

// implement device protocol
static void spi_unbind(void* ctx) {
	spi_device_t* spi = ctx;
	zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
	device_remove(spi->zxdev);
}

static void spi_release(void* ctx) {
	spi_device_t* spi = ctx;

	if (spi->regs) {
		zx_handle_close(spi->regs_handle);
		spi->regs_handle = -1;
	}

	zx_handle_close(spi->event);
	zx_handle_close(spi->dma_event);

	free(spi);
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
				cs_info->bus, cs_info->enable);

			return mtk_spi_set_cs(spi, cs_info->bus, cs_info->enable);
		}
		case IOCTL_GRT_SPI_DMA_TRANSFER: {
			grt_spi_dma_transfer_t *trans = (grt_spi_dma_transfer_t *)in_buf;
			spi_device_t *dev = spi->spi;
			zx_status_t status = ZX_OK;

			spi->data->head.spi_clk_hz = trans->trans_head->spi_clk_hz;
			spi->data->head.bus = trans->trans_head->bus;
			spi->data->head.mode = trans->trans_head->mode;
			spi->data->head.speed_hz = trans->trans_head->speed_hz;
			spi->data->head.word_delay_usecs = trans->trans_head->word_delay_usecs;
			spi->data->head.len = trans->trans_head->len;
			spi->data->head.delay_usecs = trans->trans_head->delay_usecs;
			spi->data->head.chip_select = trans->trans_head->chip_select;
			spi->data->head.bits_per_word = trans->trans_head->bits_per_word;
			spi->data->head.cs_change = trans->trans_head->cs_change;

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

			print_spi_tx_rx_data(spi->tx_addr, spi->rx_addr, spi->data->head.len);

			status = mtk_spi_transfer(spi, trans->size, spi->spi->bus_id);

		out:
			mtx_unlock(&dev->cs_lock);
			return status;
		}
		case IOCTL_GRT_STOP_SPI_DMA_TRANSFER: {
			spi->spi->stop_transfer = true;
			zxlogf(INFO, "spi: IOCTL_GRT_STOP_SPI_DMA_TRANSFER.\n");
			boot_ticks();

			return ZX_OK;
		}

	default:
		return ZX_ERR_NOT_SUPPORTED;
	}
}

static zx_status_t spi_client_close(void* ctx, uint32_t flags) {
    spi_client_device_t *client = ctx;
    spi_device_t *dev = client->spi;

    mtx_lock(&dev->cs_lock);

    /* If the client holds CS, force release */
    if (dev->cs_owner == client) {
        zxlogf(INFO, "SPI: force release CS from closing client");
        __mtk_spi_hw_set_cs(dev, 0, false);
        dev->cs_owner  = NULL;
        dev->cs_active = false;
    }

    mtx_unlock(&dev->cs_lock);

    free(client->data);
    free(client);
    return ZX_OK;
}

static void spi_client_release(void* ctx) {
    spi_client_device_t *client = ctx;
    spi_device_t *dev = client->spi;

    mtx_lock(&dev->cs_lock);

    if (dev->cs_owner == client) {
        zxlogf(INFO, "SPI: force release CS from closing client");
        __mtk_spi_hw_set_cs(dev, 0, false);
        dev->cs_owner  = NULL;
        dev->cs_active = false;
    }

    mtx_unlock(&dev->cs_lock);

    free(client->data);
    free(client);
}

static zx_protocol_device_t client_device_proto = {
	.version = DEVICE_OPS_VERSION,
	.ioctl = spi_client_ioctl,
	.close = spi_client_close,
	.release = spi_client_release,
};

static zx_status_t spi_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {

	struct spi_client_device* s = calloc(1, sizeof(struct spi_client_device));

	s->spi = ctx;

	grt_spi_data_t *spi_data = calloc(1, sizeof(grt_spi_data_t));
	grt_spi_cs_t *spi_cs = calloc(1, sizeof(grt_spi_cs_t));

	s->data = spi_data;
	s->spi_cs = spi_cs;

	device_add_args_t spi_client_args = {
		.version = DEVICE_ADD_ARGS_VERSION,
		.name = "vm-spi",
		.ctx = s,
		.ops = &client_device_proto,
		.flags = DEVICE_ADD_INSTANCE,
	};

	zx_status_t status = device_add(s->spi->zxdev, &spi_client_args, &s->device);
	if (status != ZX_OK) {
		free(s);
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

	bus[id].base = device->regs;
	bus[id].dev = device;
	device->bus_id = id;

	device->stop_transfer = false;

	mtx_init(&device->lock, mtx_plain);
	mtx_init(&device->cs_lock, mtx_plain);
	mtx_init(&device->irq_lock, mtx_plain);

	mtk_spi_init(1000000, device->bus_id, dev);

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