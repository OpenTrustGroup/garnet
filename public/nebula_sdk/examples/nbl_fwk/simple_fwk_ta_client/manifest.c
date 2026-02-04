// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* UUID : {22ac2224-a9ec-11e9-ac88-6b2db19dadcf} */
#define UUID {0x22ac2224, 0xa9ec, 0x11e9, {0xac, 0x88, 0x6b, 0x2d, 0xb1, 0x9d, 0xad, 0xcf}}

NBL_APP_MANIFEST(
	UUID,
	// Here config 10MB heap size.
	NBL_APP_CONFIG_MAX_HEAP_SIZE(10 * 1024 * 1024)
);
