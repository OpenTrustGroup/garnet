// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

// 76dfcd85-4bd7-4056-81ac-468f1b2b5be8
#define UUID {0x76dfcd85, 0x4bd7, 0x4056, {0x81, 0xac, 0x46, 0x8f, 0x1b, 0x2b, 0x5b, 0xe8}}

NBL_APP_MANIFEST(
  UUID,
  NBL_APP_TUI(0),
  NBL_APP_CONFIG_MAX_HEAP_SIZE(20*1024*1024),
);
