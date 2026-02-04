// Copyright 2021 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

/* UUID : {2a29769a-a99b-4ce4-b4ad-b54fa6856237} */
#define UUID                                                                       \
  {                                                                                \
    0x2a29769a, 0xa99b, 0x4ce4, { 0xb4, 0xad, 0xb5, 0x4f, 0xa6, 0x85, 0x62, 0x37 } \
  }

NBL_APP_MANIFEST(UUID, NBL_APP_CONFIG_MAX_HEAP_SIZE(16 * 1024 * 1024));
