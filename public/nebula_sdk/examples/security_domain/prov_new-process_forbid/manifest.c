// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {1c714264-2518-425d-afce-d1c893b8fce8} */
#define UUID {0x1c714264, 0x2518, 0x425d, {0xaf, 0xce, 0xd1, 0xc8, 0x93, 0xb8, 0xfc, 0xe8}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_NEW_PROCESS(0),
);
