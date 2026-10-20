/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>

__BEGIN_CDECLS

void writeb(u8 value, volatile void __iomem *addr);

u8 readb(const volatile void __iomem *addr);

__END_CDECLS