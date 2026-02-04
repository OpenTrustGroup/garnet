// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {5e144904-cc28-4c71-af44-6257d2a4ebb7} */
#define UUID {0x5e144904, 0xcc28, 0x4c71, {0xaf, 0x44, 0x62, 0x57, 0xd2, 0xa4, 0xeb, 0xb7}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_CONFIG_MAX_HEAP_SIZE(256*1024*1024)
);
