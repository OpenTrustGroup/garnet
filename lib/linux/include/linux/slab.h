/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/device.h>

__BEGIN_CDECLS

void *devm_kzalloc(struct device *dev, size_t size, gfp_t flags);

__END_CDECLS