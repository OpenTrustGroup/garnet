/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>

void clk_disable_unprepare(struct clk* clk) {}

unsigned long clk_get_rate(struct clk* clk) {
  return 1000;
}

int clk_notifier_register(struct clk* clk, struct notifier_block* nb) {
  return 0;
}

int clk_notifier_unregister(struct clk* clk, struct notifier_block* nb) {
  return 0;
}

int clk_prepare_enable(struct clk* clk) {
  return 0;
}

struct clk* devm_clk_get(struct device* dev, const char* id) {
  return (struct clk*)0xdeadbeef;
}