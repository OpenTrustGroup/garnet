/*
 * Copyright (c) 2020, Mediatek Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "i2c_env.h"
#include "i2c_func.h"
#include "i2c_platform.h"

#if (CURRENT_ENV == PL_I2C)
/*For Preloader*/


#elif (CURRENT_ENV == LK_I2C)
/*For LK-1.0*/
void _write_8(unsigned int addr, unsigned char value)
{
	writeb(value, (void *)addr);
}

unsigned char _read_8(unsigned int addr)
{
	return readb((void *)addr);
}

void _write_16(unsigned int addr, unsigned short value)
{
	writew(value, addr);
}

unsigned short _read_16(unsigned int addr)
{
	return readw(addr);
}

void _write_32(unsigned int addr, unsigned int value)
{
	writel(value, addr);
}

unsigned int _read_32(unsigned int addr)
{
	return readl(addr);
}

void _udelay(unsigned int us)
{
	udelay(us);
}

void _mdelay(unsigned int ms)
{
	mdelay(ms);
}

static void mtk_i2c_irq(void *cookie)
{
	I2CERR("lk not support interrupt mode!\n");
}

int _request_irq(unsigned char bus_id)
{
	I2CERR("lk not support interrupt mode!\n");
	return -EINVAL_I2C;
}

#elif (CURRENT_ENV == SCP_I2C || CURRENT_ENV == MCUPM_I2C)
/*For SCP FreeRTOS*/
void _write_8(unsigned int addr, unsigned char value)
{
	DRV_WriteReg8(addr, value);
}

unsigned char _read_8(unsigned int addr)
{
	return DRV_Reg8(addr);
}

void _write_16(unsigned int addr, unsigned short value)
{
	DRV_WriteReg16(addr, value);
}

unsigned short _read_16(unsigned int addr)
{
	return DRV_Reg16(addr);
}

void _write_32(unsigned int addr, unsigned int value)
{
	DRV_WriteReg32(addr, value);
}

unsigned int _read_32(unsigned int addr)
{
	return DRV_Reg32(addr);
}

void _udelay(unsigned int us)
{
	udelay(us);
}

void _mdelay(unsigned int ms)
{
	mdelay(ms);
}

static void mtk_i2c_irq(void *arg)
{
	uint16_t restart_flag = 0;
	uint16_t intr_stat;
	struct mtk_i2c *i2c = (struct mtk_i2c *) arg;

	BaseType_t x_higher_priority_task_woken = pdFALSE;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	intr_stat = _read_16(i2c->i2c_multi_base + OFFSET_INTR_STAT);
	_write_16(i2c->i2c_multi_base + OFFSET_INTR_STAT, intr_stat);
	/*
	 * when occurs ack error, i2c controller generate two interrupts
	 * first is the ack error interrupt, then the complete interrupt
	 * i2c->irq_stat need keep the two interrupt value.
	 */
	i2c->irq_stat |= intr_stat;
	if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag)) {
		xSemaphoreGiveFromISR(i2c->sem_comp,
				&x_higher_priority_task_woken);
		portYIELD_FROM_ISR(x_higher_priority_task_woken);

	}
}

int _request_irq(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	int ret = 0;

	if (i2c == NULL) {
		ret = -EINVAL_I2C;
		goto err_exit;
	}

	if (!i2c->sem_comp) {
		i2c->sem_comp = xSemaphoreCreateBinary();
		if (!i2c->sem_comp) {
			I2CERR("i2c create xsemaphore fail!\n");
			ret = -ENOMEM_I2C;
			goto err_exit;
		}

		if (i2c->irq.id == 0) {
			i2c->i2c_quirks[i2c->id].poll_en = true;
			I2CERR("Due to i2c%d not support interrupt mode has been switch poll mode\n",
				i2c->id);
			goto err_exit;
		}

		ret = intc_irq_request(&i2c->irq, (void *) mtk_i2c_irq, (void *)i2c);
		if (ret != 0)
			I2CERR("i2c request irq fail!\n");
	}

err_exit:
	return ret;
}

int _wait_irq_complete(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	unsigned short restart_flag = 0;
	int ret = 0;

	if (i2c == NULL) {
		ret = -EINVAL_I2C;
		goto err_exit;
	}

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	if (pdTRUE != xSemaphoreTake(i2c->sem_comp,
		2000/portTICK_RATE_MS)) {
		ret = -ETIMEDOUT_I2C;
	} else {
		if ((i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag))) {
			ret = I2C_OK;
			if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR))
				ret = -EACK_ERR_I2C;
		}
	}

err_exit:
	return ret;
}

int _lock_i2c_bus(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	int ret = I2C_OK;

	if (i2c == NULL) {
		ret = -EINVAL_I2C;
		goto err_exit;
	}

	if (!i2c->bus_lock)
		i2c->bus_lock = xSemaphoreCreateBinary();
	else if (pdTRUE != xSemaphoreTake(i2c->bus_lock, (TickType_t) 10))
		ret = -EAGAIN_I2C;

err_exit:
	return ret;
}

void _unlock_i2c_bus(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	if (i2c != NULL)
		xSemaphoreGive(i2c->bus_lock);
}

void *_malloc_memory(unsigned int size)
{
	return pvPortMalloc(size);
}

void _free_memory(void *pv)
{
	vPortFree(pv);
}

#if CURRENT_ENV == SCP_I2C
unsigned int _memory_map(void *pv)
{
	unsigned long mem_addr = 0;

	mem_addr = (unsigned long)pv;

	return (unsigned int)mem_addr;
}

void _cache_flush(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
}

void _cache_invalid(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
}
#else
unsigned int _memory_map(void *pv)
{
	unsigned long mem_addr = 0;

	mem_addr = (unsigned long)pv;

	return (unsigned int)mem_addr;
}

void _cache_flush(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
}

void _cache_invalid(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
}

#endif
#elif (CURRENT_ENV == APSYS_I2C)
/*For APSYS*/

#elif (CURRENT_ENV == ATF_I2C)
void _write_8(unsigned int addr, unsigned char value)
{
	mmio_write_8((uintptr_t)addr, value);
}

unsigned char _read_8(unsigned int addr)
{
	return mmio_read_8((uintptr_t)addr);
}

void _write_16(unsigned int addr, unsigned short value)
{
	mmio_write_16((uintptr_t)addr, value);
}

unsigned short _read_16(unsigned int addr)
{
	return mmio_read_16((uintptr_t)addr);
}

void _write_32(unsigned int addr, unsigned int value)
{
	mmio_write_32((uintptr_t)addr, value);
}

unsigned int _read_32(unsigned int addr)
{
	return mmio_read_32((uintptr_t)addr);
}

void _udelay(unsigned int us)
{
	udelay(us);
}

void _mdelay(unsigned int ms)
{
	mdelay(ms);
}

static void mtk_i2c_irq(void *cookie)
{
	uint16_t restart_flag = 0;
	uint16_t intr_stat;
	struct mtk_i2c *i2c = (struct mtk_i2c *)cookie;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	intr_stat = _read_16(i2c->i2c_multi_base + OFFSET_INTR_STAT);
	_write_16(i2c->i2c_multi_base + OFFSET_INTR_STAT, intr_stat);
	/*
	 * when occurs ack error, i2c controller generate two interrupts
	 * first is the ack error interrupt, then the complete interrupt
	 * i2c->irq_stat need keep the two interrupt value.
	 */
	 i2c->irq_stat |= intr_stat;
	if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag))
		i2c->msg_complete = true;
}

int _request_irq(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	if (i2c == NULL)
		return -EINVAL_I2C;

	return request_fiq(i2c->irq, mtk_i2c_irq, INT_EDGE_FALLING, INTR_GROUP0, (void *)i2c);
}

int _wait_irq_complete(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	unsigned short restart_flag = 0;
	int tmo_poll = 2000000;
	int ret = 0;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	while (1) {
		/*Wait i2c irq_stat*/
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

	return ret;
}

int _lock_i2c_bus(unsigned char bus_id)
{
	int ret = 0;

	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	if (i2c == NULL) {
		ret = -EINVAL_I2C;
		goto err_exit;
	}

	spin_lock(&i2c->i2c_lock);

	if (i2c->bus_lock)
		ret = -EAGAIN_I2C;
	else
		i2c->bus_lock = true;

	spin_unlock(&i2c->i2c_lock);

err_exit:
	return ret;
}

void _unlock_i2c_bus(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	if (i2c != NULL)
		i2c->bus_lock = false;
}

void *_malloc_memory(unsigned int size)
{
	/* return malloc(size); */
	return NULL;
}

void _free_memory(void *pv)
{
	/* free(pv); */
}

unsigned int _memory_map(void *pv)
{
	unsigned long mem_addr = 0;

	mem_addr = (unsigned long)pv;

	return (unsigned int)mem_addr;
}

void _cache_flush(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
}

void _cache_invalid(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
}

#elif (CURRENT_ENV == ADSP_I2C)

/*For SCP FreeRTOS*/
void _write_8(unsigned int addr, unsigned char value)
{
	DRV_WriteReg8(addr, value);
}

unsigned char _read_8(unsigned int addr)
{
	return DRV_Reg8(addr);
}

void _write_16(unsigned int addr, unsigned short value)
{
	DRV_WriteReg16(addr, value);
}

unsigned short _read_16(unsigned int addr)
{
	return DRV_Reg16(addr);
}

void _write_32(unsigned int addr, unsigned int value)
{
	DRV_WriteReg32(addr, value);
}

unsigned int _read_32(unsigned int addr)
{
	return DRV_Reg32(addr);
}

void _udelay(unsigned int us)
{
	udelay(us);
}

void _mdelay(unsigned int ms)
{
	mdelay(ms);
}

void mtk_i2c0_irq(void)
{
	uint16_t restart_flag = 0;
	uint16_t intr_stat;
	struct mtk_i2c *i2c = mtk_i2c_get_bus(I2C0);

	BaseType_t x_higher_priority_task_woken = pdFALSE;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	intr_stat = _read_16(i2c->i2c_multi_base + OFFSET_INTR_STAT);
	_write_16(i2c->i2c_multi_base + OFFSET_INTR_STAT, intr_stat);
	/*
	 * when occurs ack error, i2c controller generate two interrupts
	 * first is the ack error interrupt, then the complete interrupt
	 * i2c->irq_stat need keep the two interrupt value.
	 */
	i2c->irq_stat |= intr_stat;
	if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag)) {
		xSemaphoreGiveFromISR(i2c->sem_comp,
				&x_higher_priority_task_woken);
		portYIELD_FROM_ISR(x_higher_priority_task_woken);

	}
}

void mtk_i2c1_irq(void)
{
	uint16_t restart_flag = 0;
	uint16_t intr_stat;
	struct mtk_i2c *i2c = mtk_i2c_get_bus(I2C1);

	BaseType_t x_higher_priority_task_woken = pdFALSE;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	intr_stat = _read_16(i2c->i2c_multi_base + OFFSET_INTR_STAT);
	_write_16(i2c->i2c_multi_base + OFFSET_INTR_STAT, intr_stat);
	/*
	 * when occurs ack error, i2c controller generate two interrupts
	 * first is the ack error interrupt, then the complete interrupt
	 * i2c->irq_stat need keep the two interrupt value.
	 */
	i2c->irq_stat |= intr_stat;
	if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag)) {
		xSemaphoreGiveFromISR(i2c->sem_comp,
				&x_higher_priority_task_woken);
		portYIELD_FROM_ISR(x_higher_priority_task_woken);

	}
}

void mtk_i2c2_irq(void)
{
	uint16_t restart_flag = 0;
	uint16_t intr_stat;
	struct mtk_i2c *i2c = mtk_i2c_get_bus(I2C2);

	BaseType_t x_higher_priority_task_woken = pdFALSE;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	intr_stat = _read_16(i2c->i2c_multi_base + OFFSET_INTR_STAT);
	_write_16(i2c->i2c_multi_base + OFFSET_INTR_STAT, intr_stat);
	/*
	 * when occurs ack error, i2c controller generate two interrupts
	 * first is the ack error interrupt, then the complete interrupt
	 * i2c->irq_stat need keep the two interrupt value.
	 */
	i2c->irq_stat |= intr_stat;
	if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag)) {
		xSemaphoreGiveFromISR(i2c->sem_comp,
				&x_higher_priority_task_woken);
		portYIELD_FROM_ISR(x_higher_priority_task_woken);

	}
}

int _request_irq(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	int ret = 0;

	if (i2c == NULL) {
		ret = -EINVAL_I2C;
		goto err_exit;
	}

	if (!i2c->sem_comp) {
		i2c->sem_comp = xSemaphoreCreateBinary();
		if (!i2c->sem_comp) {
			I2CERR("i2c create xsemaphore fail!\n");
			ret = -ENOMEM_I2C;
			goto err_exit;
		}

		if (i2c->irq == 0) {
			i2c->i2c_quirks[i2c->id].poll_en = true;
			I2CERR("Due to i2c%d not support interrupt mode has been switch poll mode\n",
				i2c->id);
			goto err_exit;
		}

		switch (bus_id) {
		case 0:
			request_irq(i2c->irq, mtk_i2c0_irq, "i2c0_isr");
			break;
		case 1:
			request_irq(i2c->irq, mtk_i2c1_irq, "i2c1_isr");
			break;
		case 2:
			request_irq(i2c->irq, mtk_i2c2_irq, "i2c2_isr");
			break;
		}

		if (ret != 0)
			I2CERR("i2c request irq fail!\n");
	}

err_exit:
	return ret;
}

int _wait_irq_complete(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	unsigned short restart_flag = 0;
	int ret = 0;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	if (pdTRUE != xSemaphoreTake(i2c->sem_comp,
		2000/portTICK_RATE_MS)) {
		ret = -ETIMEDOUT_I2C;
	} else {
		if ((i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag))) {
			ret = I2C_OK;
			if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR))
				ret = -EACK_ERR_I2C;
		}
	}

	return ret;
}

int _lock_i2c_bus(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	int ret = I2C_OK;

	if (!i2c->bus_lock)
		i2c->bus_lock = xSemaphoreCreateBinary();
	else if (pdTRUE != xSemaphoreTake(i2c->bus_lock, (TickType_t) 10))
		ret = -EAGAIN_I2C;

	return ret;
}

void _unlock_i2c_bus(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	xSemaphoreGive(i2c->bus_lock);
}

void *_malloc_memory(unsigned int size)
{
	void *pv;

	pv = MTK_pvPortMalloc(size, MTK_eMemDramNormalNC);

	return pv;
}

void _free_memory(void *pv)
{
	MTK_vPortFree(pv);
}

unsigned int _memory_map(void *pv)
{
	unsigned long mem_addr = 0;

	mem_addr = (unsigned long)(pv - DRAM_MEM_OFFSET);

	return (unsigned int)mem_addr;
}

void _cache_flush(void *pv, unsigned int size)
{
	xthal_dcache_region_writeback_inv(pv, size);
}

void _cache_invalid(void *pv, unsigned int size)
{
	xthal_dcache_region_writeback_inv(pv, size);
}

#elif (CURRENT_ENV == NEBULA_I2C)
void _write_8(unsigned int addr, unsigned char value)
{
    *(volatile unsigned char *)(uintptr_t)addr = value;
}

unsigned char _read_8(unsigned int addr)
{
    return *(volatile unsigned char *)(uintptr_t)addr;
}

void _write_16(unsigned int addr, unsigned short value)
{
    *(volatile unsigned short *)(uintptr_t)addr = value;
}

unsigned short _read_16(unsigned int addr)
{
    return *(volatile unsigned short *)(uintptr_t)addr;
}

void _write_32(unsigned int addr, unsigned int value)
{
    mt_reg_sync_writel(value, (uintptr_t)addr);
}

unsigned int _read_32(unsigned int addr)
{
	return readl((uintptr_t)addr);
}

io_buffer_t tx_buf[20];
io_buffer_t rx_buf[20];

void *_malloc_memory(unsigned int size, int bus_num, bool tx_en, bool rx_en)
{
	zx_status_t status;
	io_buffer_t* buf = tx_en ? &tx_buf[bus_num] : &rx_buf[bus_num];
	if (size == 0) return NULL;
	unsigned int orig_size = size;
	if ((size & (size - 1)) != 0) {
		unsigned int pow2 = 1;
		while (pow2 < size) pow2 <<= 1;
		size = pow2;
	}

	status = io_buffer_init(buf, i2c_bti[bus_num], size,
							IO_BUFFER_RW | IO_BUFFER_CONTIG | IO_BUFFER_UNCACHED);
	if (status != ZX_OK) {
		I2CERR("io_buffer_init failed: status=%d bus=%d size=%u\n", status, bus_num, size);
		return NULL;
	}

	void* addr = io_buffer_virt(buf);
	if (!addr) {
		I2CERR("io_buffer_virt returned NULL: bus=%d\n", bus_num);
		io_buffer_release(buf);
		return NULL;
	}
	return addr;
}

void _cache_flush(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
	zx_cache_flush((void *)pv, size, ZX_CACHE_FLUSH_DATA);
}

void _cache_invalid(void *pv, unsigned int size)
{
	/* xthal_dcache_region_writeback_inv(pv, size); */
	zx_cache_flush((void *)pv, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
}

unsigned int _memory_map(int bus_num, bool tx_en, bool rx_en)
{
	uintptr_t vaddr = 0;

	if (tx_en)
		vaddr = io_buffer_phys(&tx_buf[bus_num]);
	else if (rx_en)
		vaddr = io_buffer_phys(&rx_buf[bus_num]);
	return (unsigned int)vaddr;
}

void _udelay(unsigned int us)
{
    udelay(us);
}

void _mdelay(unsigned int ms)
{
    udelay(1000 * ms);
}

// int _request_irq(unsigned char bus_id)
// {
//     struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

//     if (i2c == NULL)
//         return -EINVAL_I2C;

//     return request_fiq(i2c->irq, mtk_i2c_irq, INT_EDGE_FALLING, INTR_GROUP0, (void *)i2c);
// }

void _free_memory(int bus_num, bool tx_en, bool rx_en)
{
	if (tx_en)
		io_buffer_release(&tx_buf[bus_num]);
	else if (rx_en)
		io_buffer_release(&rx_buf[bus_num]);
}

#endif

#if (CURRENT_ENV == PL_I2C || CURRENT_ENV == LK_I2C || CURRENT_ENV == APSYS_I2C || CURRENT_ENV == NEBULA_I2C)
int _wait_irq_complete(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);
	unsigned short restart_flag = 0;
	unsigned short reg_val = 0;
	zx_signals_t observed;
	zx_status_t status;
	int ret = 0;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	status = zx_object_wait_one(i2c_dma_handler[bus_id],
								ZX_USER_SIGNAL_0,
								zx_deadline_after(ZX_MSEC(2000)),
								&observed);

	reg_val = readl(i2c->i2c_multi_base + OFFSET_INTR_STAT);
	mt_reg_sync_writel(reg_val, i2c->i2c_multi_base + OFFSET_INTR_STAT);

	if (status == ZX_OK && (observed & ZX_USER_SIGNAL_0)) {
		zx_object_signal(i2c_dma_handler[bus_id], ZX_USER_SIGNAL_0, 0);
		if (reg_val & ((I2C_HS_NACKERR | I2C_ACKERR))) {
			I2CERR("i2c ACK error!\n");
			ret = -EACK_ERR_I2C;
			goto error;
		} else if (reg_val & I2C_TRANSAC_COMP) {
			I2CERR("i2c wait irq pass\n");
			ret = I2C_OK;
		} else {
			I2CERR("i2c DMA error, reg_val is 0x%x!\n", reg_val);
			ret = ERR_I2C;
			goto error;
		}
	} else {
		I2CERR("i2c wait irq timeout, (observed=0x%x)!\n", observed);
		ret = -ETIMEDOUT_I2C;
		goto error;
	}

error:
	return ret;
}

int _lock_i2c_bus(unsigned char bus_id)
{
	int ret = 0;

	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	if (i2c == NULL) {
		ret = -EINVAL_I2C;
		goto err_exit;
	}

	if (i2c->bus_lock)
		ret = -EAGAIN_I2C;
	else
		i2c->bus_lock = true;

err_exit:
	return ret;
}

void _unlock_i2c_bus(unsigned char bus_id)
{
	struct mtk_i2c *i2c = mtk_i2c_get_bus(bus_id);

	if (i2c != NULL)
		i2c->bus_lock = false;
}
#endif

int _i2c_gpio_set(unsigned char bus_id)
{
	int ret = 0;

	ret =  _plat_i2c_gpio_set(bus_id);
	if (ret < 0)
		I2CERR("I2C%d config gpio fail\n", bus_id);

	return ret;
}

int _i2c_clk_enable(unsigned char bus_id)
{
	return _plat_i2c_clock_enable(bus_id);
}

void _i2c_clk_disable(unsigned char bus_id)
{
	_plat_i2c_clock_disable(bus_id);
}

