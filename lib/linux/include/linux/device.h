/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>
#include <linux/of_device.h>
#include <zircon/types.h>

__BEGIN_CDECLS

struct resource {
	void __iomem *vaddr;
	size_t size;
	zx_handle_t vmo_handle;
};

#define IORESOURCE_MEM 0x00000200


struct device {
	struct device_node *of_node;
	struct device *parent;
	void *platform_data; /* Platform specific data, device
					   core doesn't touch it */
	void *driver_data; /* Driver data, set and get with
					   dev_set_drvdata/dev_get_drvdata */
};

void *dev_get_platdata(struct device *dev);
void dev_set_platdata(struct device *dev, void *data);

void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res);

__END_CDECLS