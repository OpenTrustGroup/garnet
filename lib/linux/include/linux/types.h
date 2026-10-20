/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <sys/types.h>
#include <zircon/compiler.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef uint32_t __u32;
typedef uint16_t __u16;
typedef uint8_t __u8;

typedef size_t resource_size_t;
typedef unsigned int gfp_t;

#define container_of(ptr, type, member) \
  ((type*)((char*)(ptr)-offsetof(type, member)))

#define __iomem

#define GFP_KERNEL 0