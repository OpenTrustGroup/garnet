/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>
#include <stdbool.h>

__BEGIN_CDECLS

extern u32 jiffies;

#define HZ 1000

unsigned long msecs_to_jiffies(const unsigned int m);

bool time_after(unsigned long a, unsigned long b);

__END_CDECLS