/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

__BEGIN_CDECLS

struct of_device_id {
	char name[32];
	char type[32];
	char compatible[128];
	const void *data;
};

__END_CDECLS