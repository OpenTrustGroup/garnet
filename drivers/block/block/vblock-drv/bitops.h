// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(long) * 8)
#endif

static inline bool test_bit(uint8_t nr, const uint64_t *addr) {
  return ((*addr >> nr) & 1) ? 1 : 0;
}

#define BIT_MASK(x)                                                            \
  (((x) >= sizeof(unsigned long) * 8) ? (0UL - 1) : (1UL << (x)))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)

static inline void set_bit(uint8_t nr, uint64_t *addr) {
  unsigned long mask = BIT_MASK(nr);
  unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

  *p |= mask;
}
