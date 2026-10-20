/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

__BEGIN_CDECLS

#define NOTIFY_OK 0

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long event, void *data);
	struct notifier_block *next;
	int priority;
};

__END_CDECLS