/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/errno.h>
#include <linux/io.h>
#include <stdio.h>

void writeb(u8 value, volatile void __iomem* addr) {
  *(volatile uint8_t *)addr = value;
}

u8 readb(const volatile void __iomem* addr) {
  return *(volatile uint8_t *)addr;
}