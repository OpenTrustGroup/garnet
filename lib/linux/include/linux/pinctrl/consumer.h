/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/device.h>

__BEGIN_CDECLS

#define PINCTRL_STATE_DEFAULT "default"

struct pinctrl;
struct pinctrl_state;

struct pinctrl {
	int foo;
};

struct pinctrl *devm_pinctrl_get(struct device *dev);
struct pinctrl_state *pinctrl_lookup_state(struct pinctrl *p, const char *name);
int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *s);


__END_CDECLS