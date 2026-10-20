// SPDX-License-Identifier: BSD-3-Clause

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_data/i2c-imx.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <binding.h>
#include <string.h>

/* This will be the driver name the kernel reports */
#define DRIVER_NAME "imx-i2c"

/* Default value */
#define IMX_I2C_BIT_RATE	100000	/* 100kHz */

/* IMX I2C registers:
 * the I2C register offset is different between SoCs,
 * to provid support for all these chips, split the
 * register offset into a fixed base address and a
 * variable shift value, then the full register offset
 * will be calculated by
 * reg_off = ( reg_base_addr << reg_shift)
 */
#define IMX_I2C_IADR	0x00	/* i2c slave address */
#define IMX_I2C_IFDR	0x01	/* i2c frequency divider */
#define IMX_I2C_I2CR	0x02	/* i2c control */
#define IMX_I2C_I2SR	0x03	/* i2c status */
#define IMX_I2C_I2DR	0x04	/* i2c transfer data */

#define IMX_I2C_REGSHIFT	2
#define VF610_I2C_REGSHIFT	0

/* Bits of IMX I2C registers */
#define I2SR_RXAK	0x01
#define I2SR_IIF	0x02
#define I2SR_SRW	0x04
#define I2SR_IAL	0x10
#define I2SR_IBB	0x20
#define I2SR_IAAS	0x40
#define I2SR_ICF	0x80
#define I2CR_DMAEN	0x02
#define I2CR_RSTA	0x04
#define I2CR_TXAK	0x08
#define I2CR_MTX	0x10
#define I2CR_MSTA	0x20
#define I2CR_IIEN	0x40
#define I2CR_IEN	0x80

/* register bits different operating codes definition:
 * 1) I2SR: Interrupt flags clear operation differ between SoCs:
 * - write zero to clear(w0c) INT flag on i.MX,
 * - but write one to clear(w1c) INT flag on Vybrid.
 * 2) I2CR: I2C module enable operation also differ between SoCs:
 * - set I2CR_IEN bit enable the module on i.MX,
 * - but clear I2CR_IEN bit enable the module on Vybrid.
 */
#define I2SR_CLR_OPCODE_W0C	0x0
#define I2SR_CLR_OPCODE_W1C	(I2SR_IAL | I2SR_IIF)
#define I2CR_IEN_OPCODE_0	0x0
#define I2CR_IEN_OPCODE_1	I2CR_IEN

#define I2C_PM_TIMEOUT		10 /* ms */

/*
 * sorted list of clock divider, register value pairs
 * taken from table 26-5, p.26-9, Freescale i.MX
 * Integrated Portable System Processor Reference Manual
 * Document Number: MC9328MXLRM, Rev. 5.1, 06/2007
 *
 * Duplicated divider values removed from list
 */
struct imx_i2c_clk_pair {
	u16	div;
	u16	val;
};

static struct imx_i2c_clk_pair imx_i2c_clk_div[] = {
	{ 22,	0x20 }, { 24,	0x21 }, { 26,	0x22 }, { 28,	0x23 },
	{ 30,	0x00 },	{ 32,	0x24 }, { 36,	0x25 }, { 40,	0x26 },
	{ 42,	0x03 }, { 44,	0x27 },	{ 48,	0x28 }, { 52,	0x05 },
	{ 56,	0x29 }, { 60,	0x06 }, { 64,	0x2A },	{ 72,	0x2B },
	{ 80,	0x2C }, { 88,	0x09 }, { 96,	0x2D }, { 104,	0x0A },
	{ 112,	0x2E }, { 128,	0x2F }, { 144,	0x0C }, { 160,	0x30 },
	{ 192,	0x31 },	{ 224,	0x32 }, { 240,	0x0F }, { 256,	0x33 },
	{ 288,	0x10 }, { 320,	0x34 },	{ 384,	0x35 }, { 448,	0x36 },
	{ 480,	0x13 }, { 512,	0x37 }, { 576,	0x14 },	{ 640,	0x38 },
	{ 768,	0x39 }, { 896,	0x3A }, { 960,	0x17 }, { 1024,	0x3B },
	{ 1152,	0x18 }, { 1280,	0x3C }, { 1536,	0x3D }, { 1792,	0x3E },
	{ 1920,	0x1B },	{ 2048,	0x3F }, { 2304,	0x1C }, { 2560,	0x1D },
	{ 3072,	0x1E }, { 3840,	0x1F }
};

/* Vybrid VF610 clock divider, register value pairs */
static struct imx_i2c_clk_pair vf610_i2c_clk_div[] = {
	{ 20,   0x00 }, { 22,   0x01 }, { 24,   0x02 }, { 26,   0x03 },
	{ 28,   0x04 }, { 30,   0x05 }, { 32,   0x09 }, { 34,   0x06 },
	{ 36,   0x0A }, { 40,   0x07 }, { 44,   0x0C }, { 48,   0x0D },
	{ 52,   0x43 }, { 56,   0x0E }, { 60,   0x45 }, { 64,   0x12 },
	{ 68,   0x0F }, { 72,   0x13 }, { 80,   0x14 }, { 88,   0x15 },
	{ 96,   0x19 }, { 104,  0x16 }, { 112,  0x1A }, { 128,  0x17 },
	{ 136,  0x4F }, { 144,  0x1C }, { 160,  0x1D }, { 176,  0x55 },
	{ 192,  0x1E }, { 208,  0x56 }, { 224,  0x22 }, { 228,  0x24 },
	{ 240,  0x1F }, { 256,  0x23 }, { 288,  0x5C }, { 320,  0x25 },
	{ 384,  0x26 }, { 448,  0x2A }, { 480,  0x27 }, { 512,  0x2B },
	{ 576,  0x2C }, { 640,  0x2D }, { 768,  0x31 }, { 896,  0x32 },
	{ 960,  0x2F }, { 1024, 0x33 }, { 1152, 0x34 }, { 1280, 0x35 },
	{ 1536, 0x36 }, { 1792, 0x3A }, { 1920, 0x37 }, { 2048, 0x3B },
	{ 2304, 0x3C }, { 2560, 0x3D }, { 3072, 0x3E }, { 3584, 0x7A },
	{ 3840, 0x3F }, { 4096, 0x7B }, { 5120, 0x7D }, { 6144, 0x7E },
};

enum imx_i2c_type {
	IMX1_I2C,
	IMX21_I2C,
	VF610_I2C,
};

struct imx_i2c_hwdata {
	enum imx_i2c_type	devtype;
	unsigned		regshift;
	struct imx_i2c_clk_pair	*clk_div;
	unsigned		ndivs;
	unsigned		i2sr_clr_opcode;
	unsigned		i2cr_ien_opcode;
};

struct imx_i2c_struct {
	struct i2c_adapter	adapter;
	struct clk		*clk;
	struct notifier_block	clk_change_nb;
	void __iomem		*base;
	wait_queue_head_t	queue;
	unsigned long		i2csr;
	unsigned int		disable_delay;
	int			stopped;
	unsigned int		ifdr; /* IMX_I2C_IFDR */
	unsigned int		cur_clk;
	unsigned int		bitrate;
	const struct imx_i2c_hwdata	*hwdata;
	struct i2c_bus_recovery_info rinfo;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_pins_default;
	struct pinctrl_state *pinctrl_pins_gpio;
};

static const struct imx_i2c_hwdata imx1_i2c_hwdata = {
	.devtype		= IMX1_I2C,
	.regshift		= IMX_I2C_REGSHIFT,
	.clk_div		= imx_i2c_clk_div,
	.ndivs			= ARRAY_SIZE(imx_i2c_clk_div),
	.i2sr_clr_opcode	= I2SR_CLR_OPCODE_W0C,
	.i2cr_ien_opcode	= I2CR_IEN_OPCODE_1,

};

static const struct imx_i2c_hwdata imx21_i2c_hwdata = {
	.devtype		= IMX21_I2C,
	.regshift		= IMX_I2C_REGSHIFT,
	.clk_div		= imx_i2c_clk_div,
	.ndivs			= ARRAY_SIZE(imx_i2c_clk_div),
	.i2sr_clr_opcode	= I2SR_CLR_OPCODE_W0C,
	.i2cr_ien_opcode	= I2CR_IEN_OPCODE_1,

};

static struct imx_i2c_hwdata vf610_i2c_hwdata = {
	.devtype		= VF610_I2C,
	.regshift		= VF610_I2C_REGSHIFT,
	.clk_div		= vf610_i2c_clk_div,
	.ndivs			= ARRAY_SIZE(vf610_i2c_clk_div),
	.i2sr_clr_opcode	= I2SR_CLR_OPCODE_W1C,
	.i2cr_ien_opcode	= I2CR_IEN_OPCODE_0,

};

static const struct platform_device_id imx_i2c_devtype[] = {
	{
		.id = PDEV_FSL_IMX1_I2C,
		.driver_data = (kernel_ulong_t)&imx1_i2c_hwdata,
	}, {
		.id = PDEV_FSL_IMX21_I2C,
		.driver_data = (kernel_ulong_t)&imx21_i2c_hwdata,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, imx_i2c_devtype);

static const struct of_device_id i2c_imx_dt_ids[] = {
	{ .compatible = "fsl,imx1-i2c", .data = &imx1_i2c_hwdata, },
	{ .compatible = "fsl,imx21-i2c", .data = &imx21_i2c_hwdata, },
	{ .compatible = "fsl,vf610-i2c", .data = &vf610_i2c_hwdata, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, i2c_imx_dt_ids);

static inline int is_imx1_i2c(struct imx_i2c_struct *i2c_imx)
{
	return i2c_imx->hwdata->devtype == IMX1_I2C;
}

static inline void imx_i2c_write_reg(unsigned int val,
		struct imx_i2c_struct *i2c_imx, unsigned int reg)
{
	writeb(val, i2c_imx->base + (reg << i2c_imx->hwdata->regshift));
}

static inline unsigned char imx_i2c_read_reg(struct imx_i2c_struct *i2c_imx,
		unsigned int reg)
{
	return readb(i2c_imx->base + (reg << i2c_imx->hwdata->regshift));
}

static int i2c_imx_bus_busy(struct imx_i2c_struct *i2c_imx, int for_busy)
{
	unsigned long orig_jiffies = jiffies;
	unsigned int temp;

	while (1) {
		temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2SR);
		/* check for arbitration lost */
		if (temp & I2SR_IAL) {
			temp &= ~I2SR_IAL;
			imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2SR);
			return -EAGAIN;
		}

		if (for_busy && (temp & I2SR_IBB))
			break;
		if (!for_busy && !(temp & I2SR_IBB))
			break;
		if (time_after(jiffies, orig_jiffies + msecs_to_jiffies(500))) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> I2C bus is busy\n", __func__);
			return -ETIMEDOUT;
		}
		schedule();
	}

	return 0;
}

static bool check_i2csr(void* ctx) {
	struct imx_i2c_struct *i2c_imx = ctx;
	return (i2c_imx->i2csr & I2SR_IIF);
}

static int i2c_imx_trx_complete(struct imx_i2c_struct *i2c_imx)
{
	wait_event_timeout(i2c_imx->queue, i2c_imx->i2csr & I2SR_IIF, HZ / 10);

	if (unlikely(!(i2c_imx->i2csr & I2SR_IIF))) {
		return -ETIMEDOUT;
	}
	i2c_imx->i2csr = 0;
	return 0;
}

static int i2c_imx_acked(struct imx_i2c_struct *i2c_imx)
{
	if (imx_i2c_read_reg(i2c_imx, IMX_I2C_I2SR) & I2SR_RXAK) {
		dev_dbg(&i2c_imx->adapter.dev, "<%s> No ACK\n", __func__);
		return -ENXIO;  /* No ACK */
	}

	dev_dbg(&i2c_imx->adapter.dev, "<%s> ACK received\n", __func__);
	return 0;
}

static void i2c_imx_set_clk(struct imx_i2c_struct *i2c_imx,
			    unsigned int i2c_clk_rate)
{
	struct imx_i2c_clk_pair *i2c_clk_div = i2c_imx->hwdata->clk_div;
	unsigned int div;
	int i;

	/* Divider value calculation */
	if (i2c_imx->cur_clk == i2c_clk_rate)
		return;

	i2c_imx->cur_clk = i2c_clk_rate;

	div = (i2c_clk_rate + i2c_imx->bitrate - 1) / i2c_imx->bitrate;
	if (div < i2c_clk_div[0].div)
		i = 0;
	else if (div > i2c_clk_div[i2c_imx->hwdata->ndivs - 1].div)
		i = i2c_imx->hwdata->ndivs - 1;
	else
		for (i = 0; i2c_clk_div[i].div < div; i++)
			;

	/* Store divider value */
	i2c_imx->ifdr = i2c_clk_div[i].val;

	/*
	 * There dummy delay is calculated.
	 * It should be about one I2C clock period long.
	 * This delay is used in I2C bus disable function
	 * to fix chip hardware bug.
	 */
	i2c_imx->disable_delay = (500000U * i2c_clk_div[i].div
		+ (i2c_clk_rate / 2) - 1) / (i2c_clk_rate / 2);

#ifdef CONFIG_I2C_DEBUG_BUS
	dev_dbg(&i2c_imx->adapter.dev, "I2C_CLK=%d, REQ DIV=%d\n",
		i2c_clk_rate, div);
	dev_dbg(&i2c_imx->adapter.dev, "IFDR[IC]=0x%x, REAL DIV=%d\n",
		i2c_clk_div[i].val, i2c_clk_div[i].div);
#endif
}

static int i2c_imx_clk_notifier_call(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct imx_i2c_struct *i2c_imx = container_of(&ndata->clk,
						      struct imx_i2c_struct,
						      clk);

	if (action & POST_RATE_CHANGE)
		i2c_imx_set_clk(i2c_imx, ndata->new_rate);

	return NOTIFY_OK;
}

static int i2c_imx_start(struct imx_i2c_struct *i2c_imx)
{
	unsigned int temp = 0;
	int result;

	/* Set I2C clock rate */
	imx_i2c_write_reg(i2c_imx->ifdr, i2c_imx, IMX_I2C_IFDR);
	/* Enable I2C controller */
	imx_i2c_write_reg(i2c_imx->hwdata->i2sr_clr_opcode, i2c_imx, IMX_I2C_I2SR);
	imx_i2c_write_reg(i2c_imx->hwdata->i2cr_ien_opcode, i2c_imx, IMX_I2C_I2CR);

	/* Wait controller to be stable */
	usleep_range(50, 150);

	/* Start I2C transaction */
	temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
	temp |= I2CR_MSTA;
	imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
	result = i2c_imx_bus_busy(i2c_imx, 1);
	if (result)
		return result;
	i2c_imx->stopped = 0;

	temp |= I2CR_IIEN | I2CR_MTX | I2CR_TXAK;
	temp &= ~I2CR_DMAEN;
	imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
	return result;
}

static void i2c_imx_stop(struct imx_i2c_struct *i2c_imx)
{
	unsigned int temp = 0;

	if (!i2c_imx->stopped) {
		/* Stop I2C transaction */
		temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
		temp &= ~(I2CR_MSTA | I2CR_MTX);
		imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
	}
	if (is_imx1_i2c(i2c_imx)) {
		/*
		 * This delay caused by an i.MXL hardware bug.
		 * If no (or too short) delay, no "STOP" bit will be generated.
		 */
		udelay(i2c_imx->disable_delay);
	}

	if (!i2c_imx->stopped) {
		i2c_imx_bus_busy(i2c_imx, 0);
		i2c_imx->stopped = 1;
	}

	/* Disable I2C controller */
	temp = i2c_imx->hwdata->i2cr_ien_opcode ^ I2CR_IEN,
	imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
}

static irqreturn_t i2c_imx_isr(int irq, void *dev_id)
{
	struct imx_i2c_struct *i2c_imx = dev_id;
	unsigned int temp;

	temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2SR);
	if (temp & I2SR_IIF) {
		/* save status register */
		i2c_imx->i2csr = temp;
		temp &= ~I2SR_IIF;
		temp |= (i2c_imx->hwdata->i2sr_clr_opcode & I2SR_IIF);
		imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2SR);
		wake_up(&i2c_imx->queue);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int i2c_imx_write(struct imx_i2c_struct *i2c_imx, struct i2c_msg *msgs)
{
	int i, result;

	/* write slave address */
	imx_i2c_write_reg(i2c_8bit_addr_from_msg(msgs), i2c_imx, IMX_I2C_I2DR);
	result = i2c_imx_trx_complete(i2c_imx);
	if (result)
		return result;
	result = i2c_imx_acked(i2c_imx);
	if (result)
		return result;
	dev_dbg(&i2c_imx->adapter.dev, "<%s> write data\n", __func__);

	/* write data */
	for (i = 0; i < msgs->len; i++) {
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> write byte: B%d=0x%X\n",
			__func__, i, msgs->buf[i]);
		imx_i2c_write_reg(msgs->buf[i], i2c_imx, IMX_I2C_I2DR);
		result = i2c_imx_trx_complete(i2c_imx);
		if (result)
			return result;
		result = i2c_imx_acked(i2c_imx);
		if (result)
			return result;
	}
	return 0;
}

static int i2c_imx_read(struct imx_i2c_struct *i2c_imx, struct i2c_msg *msgs, bool is_lastmsg)
{
	int i, result;
	unsigned int temp;
	int block_data = msgs->flags & I2C_M_RECV_LEN;

	/* write slave address */
	imx_i2c_write_reg(i2c_8bit_addr_from_msg(msgs), i2c_imx, IMX_I2C_I2DR);
	result = i2c_imx_trx_complete(i2c_imx);
	if (result)
		return result;
	result = i2c_imx_acked(i2c_imx);
	if (result)
		return result;

	dev_dbg(&i2c_imx->adapter.dev, "<%s> setup bus\n", __func__);

	/* setup bus to read data */
	temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
	temp &= ~I2CR_MTX;

	/*
	 * Reset the I2CR_TXAK flag initially for SMBus block read since the
	 * length is unknown
	 */
	if ((msgs->len - 1) || block_data)
		temp &= ~I2CR_TXAK;
	imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
	imx_i2c_read_reg(i2c_imx, IMX_I2C_I2DR); /* dummy read */

	dev_dbg(&i2c_imx->adapter.dev, "<%s> read data\n", __func__);

	/* read data */
	for (i = 0; i < msgs->len; i++) {
		u8 len = 0;

		result = i2c_imx_trx_complete(i2c_imx);
		if (result)
			return result;
		/*
		 * First byte is the length of remaining packet
		 * in the SMBus block data read. Add it to
		 * msgs->len.
		 */
		if ((!i) && block_data) {
			len = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2DR);
			if ((len == 0) || (len > I2C_SMBUS_BLOCK_MAX))
				return -EPROTO;
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> read length: 0x%X\n",
				__func__, len);
			msgs->len += len;
		}
		if (i == (msgs->len - 1)) {
			if (is_lastmsg) {
				/*
				 * It must generate STOP before read I2DR to prevent
				 * controller from generating another clock cycle
				 */
				dev_dbg(&i2c_imx->adapter.dev,
					"<%s> clear MSTA\n", __func__);
				temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
				temp &= ~(I2CR_MSTA | I2CR_MTX);
				imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
				i2c_imx_bus_busy(i2c_imx, 0);
				i2c_imx->stopped = 1;
			} else {
				/*
				 * For i2c master receiver repeat restart operation like:
				 * read -> repeat MSTA -> read/write
				 * The controller must set MTX before read the last byte in
				 * the first read operation, otherwise the first read cost
				 * one extra clock cycle.
				 */
				temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
				temp |= I2CR_MTX;
				imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
			}
		} else if (i == (msgs->len - 2)) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> set TXAK\n", __func__);
			temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
			temp |= I2CR_TXAK;
			imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
		}
		if ((!i) && block_data)
			msgs->buf[0] = len;
		else
			msgs->buf[i] = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2DR);
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> read byte: B%d=0x%X\n",
			__func__, i, msgs->buf[i]);
	}
	return 0;
}

static int i2c_imx_xfer(struct i2c_adapter *adapter,
						struct i2c_msg *msgs, int num)
{
	unsigned int i, temp;
	int result;
	bool is_lastmsg = false;
	struct imx_i2c_struct *i2c_imx = i2c_get_adapdata(adapter);

	result = pm_runtime_get_sync(i2c_imx->adapter.dev.parent);
	if (result < 0)
		goto out;

	/* Start I2C transfer */
	result = i2c_imx_start(i2c_imx);
	if (result) {
		if (i2c_imx->adapter.bus_recovery_info) {
			i2c_recover_bus(&i2c_imx->adapter);
			result = i2c_imx_start(i2c_imx);
		}
	}

	if (result)
		goto fail0;

	/* read/write data */
	for (i = 0; i < num; i++) {
		if (i == num - 1)
			is_lastmsg = true;

		if (i) {
			temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
			temp |= I2CR_RSTA;
			imx_i2c_write_reg(temp, i2c_imx, IMX_I2C_I2CR);
			result = i2c_imx_bus_busy(i2c_imx, 1);
			if (result)
				goto fail0;
		}
		/* write/read data */
#ifdef CONFIG_I2C_DEBUG_BUS
		temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2CR);
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> CONTROL: IEN=%d, IIEN=%d, MSTA=%d, MTX=%d, TXAK=%d, RSTA=%d\n",
			__func__,
			(temp & I2CR_IEN ? 1 : 0), (temp & I2CR_IIEN ? 1 : 0),
			(temp & I2CR_MSTA ? 1 : 0), (temp & I2CR_MTX ? 1 : 0),
			(temp & I2CR_TXAK ? 1 : 0), (temp & I2CR_RSTA ? 1 : 0));
		temp = imx_i2c_read_reg(i2c_imx, IMX_I2C_I2SR);
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> STATUS: ICF=%d, IAAS=%d, IBB=%d, IAL=%d, SRW=%d, IIF=%d, RXAK=%d\n",
			__func__,
			(temp & I2SR_ICF ? 1 : 0), (temp & I2SR_IAAS ? 1 : 0),
			(temp & I2SR_IBB ? 1 : 0), (temp & I2SR_IAL ? 1 : 0),
			(temp & I2SR_SRW ? 1 : 0), (temp & I2SR_IIF ? 1 : 0),
			(temp & I2SR_RXAK ? 1 : 0));
#endif
		if (msgs[i].flags & I2C_M_RD) {
			result = i2c_imx_read(i2c_imx, &msgs[i], is_lastmsg);
		} else {
			result = i2c_imx_write(i2c_imx, &msgs[i]);
		}
		if (result)
			goto fail0;
	}

fail0:
	/* Stop I2C transfer */
	i2c_imx_stop(i2c_imx);

	pm_runtime_mark_last_busy(i2c_imx->adapter.dev.parent);
	pm_runtime_put_autosuspend(i2c_imx->adapter.dev.parent);

out:
	return (result < 0) ? result : num;
}

static void i2c_imx_prepare_recovery(struct i2c_adapter *adap)
{
	struct imx_i2c_struct *i2c_imx;

	i2c_imx = container_of(adap, struct imx_i2c_struct, adapter);

	pinctrl_select_state(i2c_imx->pinctrl, i2c_imx->pinctrl_pins_gpio);
}

static void i2c_imx_unprepare_recovery(struct i2c_adapter *adap)
{
	struct imx_i2c_struct *i2c_imx;

	i2c_imx = container_of(adap, struct imx_i2c_struct, adapter);

	pinctrl_select_state(i2c_imx->pinctrl, i2c_imx->pinctrl_pins_default);
}

/*
 * We switch SCL and SDA to their GPIO function and do some bitbanging
 * for bus recovery. These alternative pinmux settings can be
 * described in the device tree by a separate pinctrl state "gpio". If
 * this is missing this is not a big problem, the only implication is
 * that we can't do bus recovery.
 */
static int i2c_imx_init_recovery_info(struct imx_i2c_struct *i2c_imx,
		struct platform_device *pdev)
{
	struct i2c_bus_recovery_info *rinfo = &i2c_imx->rinfo;

	i2c_imx->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!i2c_imx->pinctrl || IS_ERR(i2c_imx->pinctrl)) {
		return PTR_ERR(i2c_imx->pinctrl);
	}

	i2c_imx->pinctrl_pins_default = pinctrl_lookup_state(i2c_imx->pinctrl,
			PINCTRL_STATE_DEFAULT);
	i2c_imx->pinctrl_pins_gpio = pinctrl_lookup_state(i2c_imx->pinctrl,
			"gpio");
	rinfo->sda_gpiod = devm_gpiod_get(&pdev->dev, "sda", GPIOD_IN);
	rinfo->scl_gpiod = devm_gpiod_get(&pdev->dev, "scl", GPIOD_OUT_HIGH_OPEN_DRAIN);

	if (PTR_ERR(rinfo->sda_gpiod) == -EPROBE_DEFER ||
	    PTR_ERR(rinfo->scl_gpiod) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(rinfo->sda_gpiod) ||
		   IS_ERR(rinfo->scl_gpiod) ||
		   IS_ERR(i2c_imx->pinctrl_pins_default) ||
		   IS_ERR(i2c_imx->pinctrl_pins_gpio)) {
		dev_dbg(&pdev->dev, "recovery information incomplete\n");
		return 0;
	}

	dev_dbg(&pdev->dev, "using scl%s for recovery\n",
		rinfo->sda_gpiod ? ",sda" : "");

	rinfo->prepare_recovery = i2c_imx_prepare_recovery;
	rinfo->unprepare_recovery = i2c_imx_unprepare_recovery;
	rinfo->recover_bus = i2c_generic_scl_recovery;
	i2c_imx->adapter.bus_recovery_info = rinfo;

	return 0;
}

static u32 i2c_imx_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL
		| I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}

static const struct i2c_algorithm i2c_imx_algo = {
	.master_xfer	= i2c_imx_xfer,
	.functionality	= i2c_imx_func,
};

static int i2c_imx_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id = of_match_device(i2c_imx_dt_ids,
							   &pdev->dev);
	struct imx_i2c_struct *i2c_imx;
	struct resource *res;
	struct imxi2c_platform_data *pdata = dev_get_platdata(&pdev->dev);
	void __iomem *base;
	int irq, ret;

	dev_dbg(&pdev->dev, "<%s>\n", __func__);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "can't get irq number\n");
		return irq;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	i2c_imx = devm_kzalloc(&pdev->dev, sizeof(*i2c_imx), GFP_KERNEL);
	if (!i2c_imx)
		return -ENOMEM;

	if (of_id) {
		i2c_imx->hwdata = of_id->data;
	} else {
		i2c_imx->hwdata = (struct imx_i2c_hwdata *)
				platform_get_device_id(pdev)->driver_data;
	}

	/* Setup i2c_imx driver structure */
	strlcpy(i2c_imx->adapter.name, pdev->name, sizeof(i2c_imx->adapter.name));
	i2c_imx->adapter.algo		= &i2c_imx_algo;
	i2c_imx->adapter.dev.parent	= &pdev->dev;
	i2c_imx->adapter.nr		= pdev->id;
	i2c_imx->adapter.dev.of_node	= pdev->dev.of_node;
	i2c_imx->base			= base;
	/* Get I2C clock */
	i2c_imx->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c_imx->clk)) {
		dev_err(&pdev->dev, "can't get I2C clock\n");
		return PTR_ERR(i2c_imx->clk);
	}

	ret = clk_prepare_enable(i2c_imx->clk);
	if (ret) {
		dev_err(&pdev->dev, "can't enable I2C clock, ret=%d\n", ret);
		return ret;
	}

	/* Request IRQ */
	ret = devm_request_irq(&pdev->dev, irq, i2c_imx_isr, IRQF_SHARED,
				pdev->name, i2c_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't claim irq %d\n", irq);
		goto clk_disable;
	}

	/* Init queue */
	init_waitqueue_head(&i2c_imx->queue);

	/* Set up adapter data */
	i2c_set_adapdata(&i2c_imx->adapter, i2c_imx);

	/* Set up platform driver data */
	platform_set_drvdata(pdev, i2c_imx);

	pm_runtime_set_autosuspend_delay(&pdev->dev, I2C_PM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		goto rpm_disable;

	/* Set up clock divider */
	i2c_imx->bitrate = IMX_I2C_BIT_RATE;
	ret = of_property_read_u32(pdev->dev.of_node,
				   "clock-frequency", &i2c_imx->bitrate);
	if (ret < 0 && pdata && pdata->bitrate)
		i2c_imx->bitrate = pdata->bitrate;
	i2c_imx->clk_change_nb.notifier_call = i2c_imx_clk_notifier_call;
	clk_notifier_register(i2c_imx->clk, &i2c_imx->clk_change_nb);
	i2c_imx_set_clk(i2c_imx, clk_get_rate(i2c_imx->clk));

	/* Set up chip registers to defaults */
	imx_i2c_write_reg(i2c_imx->hwdata->i2cr_ien_opcode ^ I2CR_IEN,
			i2c_imx, IMX_I2C_I2CR);
	imx_i2c_write_reg(i2c_imx->hwdata->i2sr_clr_opcode, i2c_imx, IMX_I2C_I2SR);

	/* Init optional bus recovery function */
	ret = i2c_imx_init_recovery_info(i2c_imx, pdev);
	/* Give it another chance if pinctrl used is not ready yet */
	if (ret == -EPROBE_DEFER)
		goto clk_notifier_unregister;

	/* Add I2C adapter */
	ret = i2c_add_numbered_adapter(&i2c_imx->adapter);
	if (ret < 0)
		goto clk_notifier_unregister;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	dev_dbg(&i2c_imx->adapter.dev, "claimed irq %d\n", irq);
	dev_dbg(&i2c_imx->adapter.dev, "device resources: %pR\n", res);
	dev_dbg(&i2c_imx->adapter.dev, "adapter name: \"%s\"\n",
		i2c_imx->adapter.name);
	dev_info(&i2c_imx->adapter.dev, "IMX I2C adapter registered\n");

	return 0;   /* Return OK */

clk_notifier_unregister:
	clk_notifier_unregister(i2c_imx->clk, &i2c_imx->clk_change_nb);
rpm_disable:
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

clk_disable:
	clk_disable_unprepare(i2c_imx->clk);
	return ret;
}
PLATFORM_DRIVER_ENTRY(i2c_imx_probe);

static int i2c_imx_remove(struct platform_device *pdev)
{
	struct imx_i2c_struct *i2c_imx = platform_get_drvdata(pdev);
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		return ret;

	/* remove adapter */
	dev_dbg(&i2c_imx->adapter.dev, "adapter removed\n");
	i2c_del_adapter(&i2c_imx->adapter);

	/* setup chip registers to defaults */
	imx_i2c_write_reg(0, i2c_imx, IMX_I2C_IADR);
	imx_i2c_write_reg(0, i2c_imx, IMX_I2C_IFDR);
	imx_i2c_write_reg(0, i2c_imx, IMX_I2C_I2CR);
	imx_i2c_write_reg(0, i2c_imx, IMX_I2C_I2SR);

	clk_notifier_unregister(i2c_imx->clk, &i2c_imx->clk_change_nb);
	clk_disable_unprepare(i2c_imx->clk);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int i2c_imx_runtime_suspend(struct device *dev)
{
	struct imx_i2c_struct *i2c_imx = dev_get_drvdata(dev);

	clk_disable(i2c_imx->clk);

	return 0;
}

static int i2c_imx_runtime_resume(struct device *dev)
{
	struct imx_i2c_struct *i2c_imx = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(i2c_imx->clk);
	if (ret)
		dev_err(dev, "can't enable I2C clock, ret=%d\n", ret);

	return ret;
}

static const struct dev_pm_ops i2c_imx_pm_ops = {
	SET_RUNTIME_PM_OPS(i2c_imx_runtime_suspend,
			   i2c_imx_runtime_resume, NULL)
};
#define I2C_IMX_PM_OPS (&i2c_imx_pm_ops)
#else
#define I2C_IMX_PM_OPS NULL
#endif /* CONFIG_PM */

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = ddk_init,
    .bind = ddk_bind,
    .release = ddk_release,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(i2c-imx, driver_ops, "i2c-imx", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_FSL_IMX1_I2C),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_FSL_IMX21_I2C),
ZIRCON_DRIVER_END(i2c-imx)