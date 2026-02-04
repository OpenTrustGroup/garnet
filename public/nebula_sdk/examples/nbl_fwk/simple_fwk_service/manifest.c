// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* UUID : {17d178d2-0810-11e9-875a-fb708664c5f4} */
#define UUID {0x17d178d2, 0x0810, 0x11e9, {0x87, 0x5a, 0xfb, 0x70, 0x86, 0x64, 0xc5, 0xf4}}

NBL_APP_MANIFEST(
	UUID,
	// Here config 10MB heap size.
	NBL_APP_CONFIG_MAX_HEAP_SIZE(10 * 1024 * 1024)
);
