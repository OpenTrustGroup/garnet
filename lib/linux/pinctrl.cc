/* SPDX-License-Identifier: GPL-2.0 */


#include <linux/errno.h>
#include <linux/pinctrl/consumer.h>

struct pinctrl* devm_pinctrl_get(struct device* dev) {
  return nullptr;
}

struct pinctrl_state* pinctrl_lookup_state(struct pinctrl* p,
                                           const char* name) {
  return nullptr;
}

int pinctrl_select_state(struct pinctrl* p, struct pinctrl_state* s) {
  return -ENOTSUP;
}