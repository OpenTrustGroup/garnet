// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

/* App UUID:   {9f1ab717-8a27-43ec-bfa9-633b274d6251} */
#define UUID {0x9f1ab717, 0x8a27, 0x43ec, {0xbf, 0xa9, 0x63, 0x3b, 0x27, 0x4d, 0x62, 0x51}}

NBL_APP_MANIFEST(
	UUID,
	NBL_APP_NEW_PROCESS(1),
);
