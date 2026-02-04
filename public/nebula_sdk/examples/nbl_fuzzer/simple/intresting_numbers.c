// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Generate number like random for argument.
 */

#include "fuz_common.h"
#include <stdio.h>
#include <stdlib.h>

#define PAGE_OFFSET 0xffffffc000000000UL

static unsigned int plus_minus_two(unsigned int num) {
  /* Now munge it for off-by-ones. */
  switch (fuz_rnd() % 4) {
  case 0:
    num -= 2;
    break;
  case 1:
    num -= 1;
    break;
  case 2:
    num += 1;
    break;
  case 3:
    num += 2;
    break;
  }
  return num;
}

static unsigned char get_interesting_8bit_value(void) {
  switch (fuz_rnd() % 5) {
  case 0:
    return 1; // one
  case 1:
    return 0xff; // max
  case 2:
    return 1UL << (fuz_rnd() & 7); // 2^n (1 -> 128)
  case 3:
    return RAND_BYTE(); // 0 -> 0xff
  default:
    return 0; // zero
  }
}

static unsigned short get_interesting_16bit_value(void) {
  switch (fuz_rnd() % 4) {
  case 0:
    return 0x8000 >> (fuz_rnd() & 7); // 2^n (0x100 -> 0x8000)
  case 1:
    return fuz_rnd() & 0xffff; // 0 -> 0xffff
  case 2:
    return 0xff00 | RAND_BYTE(); // 0xff00 -> 0xffff
  default:
    return 0xffff; // max
  }
}

static unsigned int get_interesting_32bit_value(void) {
  switch (fuz_rnd() % 10) {
  case 0:
    return 0x80000000 >> (fuz_rnd() & 0x1f); // 2^n (1 -> 0x10000)
  case 1:
    return fuz_rnd(); // 0 -> RAND_MAX (likely 0x7fffffff)
  case 2:
    return (unsigned int)0xff << (4 * (fuz_rnd() % 7));
  case 3:
    return 0xffff0000;
  case 4:
    return 0xffffe000;
  case 5:
    return 0xffffff00 | RAND_BYTE();
  case 6:
    return 0xffffffff - PAGE_SIZE;
  case 7:
    return PAGE_SIZE;
  case 8:
    return PAGE_SIZE * ((fuz_rnd() % (0xffffffff / PAGE_SIZE)) + 1);
  default:
    return 0xffffffff; // max
  }
}

#if WORD_BIT != 32
static unsigned long per_arch_interesting_addr(unsigned long low) {
  int i = 0;

#if defined(__x86_64__)
  i = fuz_rnd() % 4;

  switch (i) {
  case 0:
    return 0x00007fffffffffffUL; // x86-64 canonical addr end.
  case 1:
    return 0x0000800000000000UL; // First x86-64 non-canonical addr
  case 2:
    return 0xffff800000000000UL |
           (low << 4); // x86-64 canonical addr range 2 begin
  case 3:
    return VDSO_ADDR | (low & 0x0fffff);
  }
#endif

  // FIXME: Add more arch specific addresses here.

  return i | low;
}
#endif /* WORD_BIT */

unsigned long get_interesting_value(void) {
  unsigned long low = 0;

  switch (fuz_rnd() % 3) {
  case 0:
    low = get_interesting_8bit_value();
    break;
  case 1:
    low = get_interesting_16bit_value();
    break;
  case 2:
    low = get_interesting_32bit_value();
    break;
  }

  low = (fuz_rnd() & 0xf) ? low
                          : plus_minus_two(low); // 1 in 16 call plus_minus_two
#if WORD_BIT != 32

  if (RAND_BOOL()) { // FIXME: This should likely be less aggressive than 50/50
    switch (fuz_rnd() % 9) {
    case 0:
      return 0x0000000100000000UL | low;
    case 1:
      return 0x7fffffff00000000UL | low;
    case 2:
      return 0x8000000000000000UL | low;
    case 3:
      return 0xffffffff00000000UL | low;
    case 4:
      return 0xffffffffffffff00UL | RAND_BYTE();
    case 5:
      return 0xffffffffffffffffUL - PAGE_SIZE;
    case 6:
      return PAGE_OFFSET | (low << 4);
    case 7:
      return per_arch_interesting_addr(low);
    case 8:
      return (low << 32);
    }
  }

#endif /* WORD_BIT */
  return low;
}
