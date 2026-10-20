/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/errno.h>

__BEGIN_CDECLS

#define PAGE_SIZE (4096)
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define IS_ERR(ptr)  ((unsigned long)(ptr) >= (unsigned long)-MAX_ERRNO)

#define PTR_ERR(ptr)  ((long)(ptr) & ~PAGE_MASK)
#define ERR_PTR(err) ((void *)((long)(err)))

__END_CDECLS