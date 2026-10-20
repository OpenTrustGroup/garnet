/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <ddk/device.h>
#include <linux/device.h>
#include <linux/types.h>

__BEGIN_CDECLS

struct gpio_desc;
struct pinctrl;
struct pinctrl_state;
struct i2c_adapter;
struct i2c_bus_recovery_info {
	int (*recover_bus)(struct i2c_adapter *adap);
	int (*get_scl)(struct i2c_adapter *adap);
	void (*set_scl)(struct i2c_adapter *adap, int val);
	int (*get_sda)(struct i2c_adapter *adap);
	void (*set_sda)(struct i2c_adapter *adap, int val);
	int (*get_bus_free)(struct i2c_adapter *adap);
	void (*prepare_recovery)(struct i2c_adapter *adap);
	void (*unprepare_recovery)(struct i2c_adapter *adap);
	/* gpio recovery */
	struct gpio_desc *scl_gpiod;
	struct gpio_desc *sda_gpiod;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_gpio;
};

#define I2C_FUNC_I2C  0x00000001
#define I2C_FUNC_SMBUS_EMUL  0x00000400
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA  0x00000800
#define I2C_SMBUS_BLOCK_MAX 32

#define I2C_M_RD 0x0001
#define I2C_M_RECV_LEN 0x0400

int i2c_generic_scl_recovery(struct i2c_adapter *adap);

struct i2c_msg {
	__u16 addr;        // I2C address of the target device
	__u16 flags;       // Flags for read/write, etc.
	__u16 len;         // Length of the data (in bytes)
	__u8 *buf;         // Pointer to the buffer containing the data (for read/write)
};

union i2c_smbus_data {
	__u8  byte;
	__u16 word;
	__u8  block[I2C_SMBUS_BLOCK_MAX + 2]; /* block[0] is used for length */
					       /* block[1] is first byte of data */
					       /* block[2] is second byte, and so on */
};

static inline u8 i2c_8bit_addr_from_msg(const struct i2c_msg *msg)
{
    return (msg->addr << 1) | (msg->flags & I2C_M_RD ? 1 : 0);
}

struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	unsigned int (*functionality)(struct i2c_adapter *adap);
	int (*smbus_xfer)(struct i2c_adapter *adap, u16 addr, unsigned short flags,
			  char read_write, u8 command, int size, union i2c_smbus_data *data);
};


struct i2c_adapter {
	char name[48];
	int nr;
	struct device dev;
	struct i2c_bus_recovery_info *bus_recovery_info;
	const struct i2c_algorithm *algo;
};

/* i2c */

#define I2C_NAME_SIZE	20
#define I2C_MODULE_PREFIX "i2c:"

struct i2c_device_id {
	char name[I2C_NAME_SIZE];
	uint64_t driver_data;	/* Data private to the driver */
};

struct list_head {
	struct list_head *next, *prev;
};

struct i2c_client;

enum i2c_slave_event {
	I2C_SLAVE_READ_REQUESTED,
	I2C_SLAVE_WRITE_REQUESTED,
	I2C_SLAVE_READ_PROCESSED,
	I2C_SLAVE_WRITE_RECEIVED,
	I2C_SLAVE_STOP,
};

typedef int (*i2c_slave_cb_t)(struct i2c_client *client,
			      enum i2c_slave_event event, u8 *val);

struct i2c_client {
	unsigned short flags;		/* div., see below		*/
#define I2C_CLIENT_PEC		0x04	/* Use Packet Error Checking */
#define I2C_CLIENT_TEN		0x10	/* we have a ten bit chip address */
					/* Must equal I2C_M_TEN below */
#define I2C_CLIENT_SLAVE	0x20	/* we are the slave */
#define I2C_CLIENT_HOST_NOTIFY	0x40	/* We want to use I2C host notify */
#define I2C_CLIENT_WAKE		0x80	/* for board_info; true iff can wake */
#define I2C_CLIENT_SCCB		0x9000	/* Use Omnivision SCCB protocol */
					/* Must match I2C_M_STOP|IGNORE_NAK */

	unsigned short addr;		/* chip address - NOTE: 7bit	*/
					/* addresses are stored in the	*/
					/* _LOWER_ 7 bits		*/
	char name[I2C_NAME_SIZE];
	struct i2c_adapter *adapter;	/* the adapter we sit on	*/
	struct device dev;		/* the device structure		*/
	int init_irq;			/* irq set at initialization	*/
	int irq;			/* irq issued by device		*/
	struct list_head detected;
	i2c_slave_cb_t slave_cb;	/* callback for slave mode	*/
	zx_device_t* parent;
};

void *i2c_get_adapdata(struct i2c_adapter *adap);
int i2c_del_adapter(struct i2c_adapter *adap);
int i2c_add_numbered_adapter(struct i2c_adapter *adap);
int i2c_recover_bus(struct i2c_adapter *adap);
void i2c_set_adapdata(struct i2c_adapter *adapter, void *data);
int i2c_transfer(struct i2c_adapter* adap, struct i2c_msg* msg, int num);

__END_CDECLS