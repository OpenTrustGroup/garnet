/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <sys/types.h>
#include <stdlib.h>
#include <linux/device.h>
#include <linux/irqreturn.h>

__BEGIN_CDECLS

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef uint64_t kernel_ulong_t;

struct platform_device_id {
	uint16_t id;
	kernel_ulong_t driver_data;
};

struct platform_device {
	const char *name;
	int id;
	struct device dev;
	const struct platform_device_id *id_entry;
};

void dev_dbg(const struct device *dev, const char *fmt, ...);
void dev_err(const struct device *dev, const char *fmt, ...);
void dev_info(const struct device *dev, const char *fmt, ...);

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->driver_data;
}

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}

struct resource *platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num);
int platform_get_irq(struct platform_device *pdev, unsigned int num);

struct platform_device_id *platform_get_device_id(struct platform_device *pdev);
void *platform_get_drvdata(const struct platform_device *pdev);
void platform_set_drvdata(struct platform_device *pdev, void *data);

__END_CDECLS
