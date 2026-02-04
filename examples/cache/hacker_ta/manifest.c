// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* UUID : {d5ad329a-151f-4c12-82e5-72ad54bf7b73} */
#define UUID {0xd5ad329a, 0x151f, 0x4c12, {0x82, 0xe5, 0x72, 0xad, 0x54, 0xbf, 0x7b, 0x73}}

NBL_APP_MANIFEST(
	UUID,
	// Here config 10MB heap size.
	NBL_APP_CONFIG_MAX_HEAP_SIZE(10 * 1024 * 1024)
);
