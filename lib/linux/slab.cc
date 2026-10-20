/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/slab.h>
#include <stdlib.h>
#include <string.h>

void* devm_kzalloc(struct device* dev, size_t size, gfp_t flags) {
  void* p = malloc(size);

  if (p) {
    memset(p, 0, size);
    return p;
  }

  return nullptr;
}