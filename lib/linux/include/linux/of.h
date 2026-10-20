/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>
#include <linux/device.h>
#include <linux/of_device.h>

__BEGIN_CDECLS

struct device_node {
	int foo;
};

const struct of_device_id *of_match_device(const struct of_device_id *matches, struct device *dev);
int of_property_read_u32(const struct device_node *np, const char *propname, u32 *out_value);

__END_CDECLS