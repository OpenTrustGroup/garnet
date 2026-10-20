/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/gpio/consumer.h>
#include <linux/errno.h>

struct gpio_desc *devm_gpiod_get(struct device *dev,
	const char *con_id,
	enum gpiod_flags flags) {
	return nullptr;
}