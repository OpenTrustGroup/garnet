// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID: {1b0623ec-86a3-4d94-bea3-1050889e6c98} */
#define UUID {0x1b0623ec, 0x86a3, 0x4d94, {0xbe, 0xa3, 0x10, 0x50, 0x88, 0x9e, 0x6c, 0x98}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_AUTO_START(0),
	NBL_APP_NEW_PROCESS(1)
);
