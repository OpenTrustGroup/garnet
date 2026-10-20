/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/types.h>
#include <linux/delay.h>
#include <unistd.h>

void usleep_range(unsigned long min, unsigned long max) {}

void udelay(unsigned long usecs) {
    usleep(usecs);
}

unsigned long msecs_to_jiffies(const unsigned int m) {
    return m / (1000 / HZ);
}

bool time_after(unsigned long a, unsigned long b) {
    return a > b;
}