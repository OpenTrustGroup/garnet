// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>
//uuid 65317e4c-a4c3-43a9-9f45-899de8eef7f0
#define UUID {0x65317e4c, 0xa4c3, 0x43a9, {0x9f, 0x45, 0x89, 0x9d, 0xe8, 0xee, 0xf7, 0xf0}}

NBL_APP_MANIFEST(
   UUID,
   NBL_APP_CONFIG_MAX_HEAP_SIZE(35*1024*1024),
   NBL_APP_TUI(1),
);
