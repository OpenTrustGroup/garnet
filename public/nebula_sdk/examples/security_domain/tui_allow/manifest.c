// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stddef.h>
#include <nbl_app_manifest.h>

// 0e9026cb-3dfd-4b5a-98f1-44765cdf5171
#define UUID {0x0e9026cb, 0x3dfd, 0x4b5a, {0x98, 0xf1, 0x44, 0x76, 0x5c, 0xdf, 0x51, 0x71}}

NBL_APP_MANIFEST(
  UUID,
  NBL_APP_TUI(1),
  NBL_APP_CONFIG_MAX_HEAP_SIZE(20*1024*1024),
);
