/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>

__BEGIN_CDECLS

// Sized opaque struct so it can be stack-allocated in C
typedef struct wait_queue_head {
    char __opaque[64]; // adjust to sizeof(actual C++ struct)
} wait_queue_head_t;

void init_waitqueue_head(wait_queue_head_t *wq);
void wake_up(wait_queue_head_t *wq);
long __wait_event_timeout_impl(wait_queue_head_t *wq, long timeout_ms);

// This macro works in both C and C++
#define wait_event_timeout(wq, condition, timeout_ms)          \
({                                                             \
    long __ret = 0;                                            \
    if (!(condition)) {                                        \
        __ret = __wait_event_timeout_impl(&(wq), (timeout_ms)); \
    } else {                                                   \
        __ret = (timeout_ms);                                  \
    }                                                          \
    __ret;                                                     \
})

__END_CDECLS
