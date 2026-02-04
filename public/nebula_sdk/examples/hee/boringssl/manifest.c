// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {67c29ae7-5e12-462b-9cc6-efc31c345f77} */
#define UUID {0x67c29ae7, 0x5e12, 0x462b, {0x9c, 0xc6, 0xef, 0xc3, 0x1c, 0x34, 0x57, 0x77}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_CONFIG_MAX_HEAP_SIZE(256*1024*1024)
);
