/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/errno.h>
#include <linux/of.h>

const struct of_device_id* of_match_device(const struct of_device_id* matches,
                                           struct device* dev) {
  return nullptr;
}
int of_property_read_u32(const struct device_node* np,
                         const char* propname,
                         u32* out_value) {
  return -ENOTSUP;
}