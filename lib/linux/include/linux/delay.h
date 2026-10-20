/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/jiffies.h>

__BEGIN_CDECLS

void usleep_range(unsigned long min, unsigned long max);

void udelay(unsigned long usecs);

__END_CDECLS