// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {5cc8dc88-b561-404c-8475-517aed1c241c} */
#define UUID {0x5cc8dc88, 0xb561, 0x404c, {0x84, 0x75, 0x51, 0x7a, 0xed, 0x1c, 0x24, 0x1c}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_CALL_TRUSTY_API(0),
);
