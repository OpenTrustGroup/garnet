// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>
//uuid c5db5914-d147-4b69-addb-cd07d2c563bc
#define UUID {0xc5db5914, 0xd147, 0x4b69, {0xad, 0xdb, 0xcd, 0x07, 0xd2, 0xc5, 0x63, 0xbc}}

NBL_APP_MANIFEST(
   UUID,
   NBL_APP_CONFIG_MAX_HEAP_SIZE(26*1024*1024),
   NBL_APP_TUI(1),
);
