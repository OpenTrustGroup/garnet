/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/device.h>

void* dev_get_platdata(struct device* dev) {
  return dev->platform_data;
}

void dev_set_platdata(struct device *dev, void *data) {
	dev->platform_data = data;
}

void __iomem* devm_ioremap_resource(struct device* dev,
                                    const struct resource* res) {
  if (!res)
    return nullptr;

  return res->vaddr;
}