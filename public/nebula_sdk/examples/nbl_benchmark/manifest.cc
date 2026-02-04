// Copyright 2021 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

/* UUID : {82207a35-09c5-4c78-9ff7-61491c8a69e8} */
#define UUID                                                                       \
  {                                                                                \
    0x82207a35, 0x09c5, 0x4c78, { 0x9f, 0xf7, 0x61, 0x49, 0x1c, 0x8a, 0x69, 0xe8 } \
  }

NBL_APP_MANIFEST(UUID, NBL_APP_CONFIG_MAX_HEAP_SIZE(16 * 1024 * 1024));
