/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>

__BEGIN_CDECLS

enum gpiod_flags {
	GPIOD_IN = 0x01,                /* Input */
	GPIOD_OUT_LOW = 0x02,           /* Output, initially low */
	GPIOD_OUT_HIGH = 0x04,          /* Output, initially high */
	GPIOD_OPEN_DRAIN = 0x08,        /* Open-drain output */
	GPIOD_OPEN_SOURCE = 0x10,       /* Open-source output */
	GPIOD_OUT_LOW_OPEN_DRAIN = 0x0A,/* Output low + open-drain */
	GPIOD_OUT_HIGH_OPEN_DRAIN = 0x0C,/* Output high + open-drain */
	GPIOD_OUT_LOW_OPEN_SOURCE = 0x12,/* Output low + open-source */
	GPIOD_OUT_HIGH_OPEN_SOURCE = 0x14/* Output high + open-source */
};

struct gpio_device;
struct gpio_desc {
	struct gpio_device	*gdev;
	unsigned long		flags;
	const char		*label;
};

struct gpio_desc *devm_gpiod_get(struct device *dev,
	const char *con_id,
	enum gpiod_flags flags);

__END_CDECLS