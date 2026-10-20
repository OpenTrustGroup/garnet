/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/device.h>
#include <linux/irqreturn.h>

__BEGIN_CDECLS

#define IRQF_SHARED 0

typedef irqreturn_t (*irq_handler_t)(int, void *);

int devm_request_irq(struct device *dev,
	unsigned int irq,
	irq_handler_t handler,
	unsigned long flags,
	const char *name,
	void *dev_id);

__END_CDECLS