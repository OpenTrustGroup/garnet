// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* UUID : {1368f3b1-26c5-45a6-ad3d-1d3b84331328} */
#define UUID {0x1368f3b1, 0x26c5, 0x45a6, {0xad, 0x3d, 0x1d, 0x3b, 0x84, 0x33, 0x13, 0x28}}

NBL_APP_MANIFEST(
	UUID,
	// Here config 10MB heap size.
	NBL_APP_CONFIG_MAX_HEAP_SIZE(10 * 1024 * 1024)
);
