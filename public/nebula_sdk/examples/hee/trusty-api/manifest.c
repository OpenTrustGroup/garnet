// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {fbed7a6a-2b57-4b3c-a9c2-75e992ce603d} */
#define UUID {0xfbed7a6a, 0x2b57, 0x4b3c, {0xa9, 0xc2, 0x75, 0xe9, 0x92, 0xc3, 0x60, 0x3d}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_CALL_TRUSTY_API(1),
);
