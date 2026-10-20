/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

__BEGIN_CDECLS

enum irqreturn {
    IRQ_NONE = 0,
    IRQ_HANDLED,
    IRQ_WAKE_THREAD,
};

typedef enum irqreturn irqreturn_t;

__END_CDECLS