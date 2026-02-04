// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {882940aa-8666-4f7a-9527-697166dd3662} */
#define UUID {0x882940aa, 0x8666, 0x4f7a, {0x95, 0x27, 0x69, 0x71, 0x66, 0xdd, 0x36, 0x62}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_AUTO_START(0),
);
