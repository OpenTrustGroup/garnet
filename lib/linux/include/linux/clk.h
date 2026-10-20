/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/device.h>

__BEGIN_CDECLS

#define POST_RATE_CHANGE 1

struct clk {
	int foo;
};

struct clk_notifier_data {
	struct clk *clk;
	unsigned long old_rate;
	unsigned long new_rate;
};

void clk_disable_unprepare(struct clk *clk);
unsigned long clk_get_rate(struct clk *clk);
int clk_notifier_register(struct clk *clk, struct notifier_block *nb);
int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb);
int clk_prepare_enable(struct clk *clk);
struct clk *devm_clk_get(struct device *dev, const char *id);


__END_CDECLS