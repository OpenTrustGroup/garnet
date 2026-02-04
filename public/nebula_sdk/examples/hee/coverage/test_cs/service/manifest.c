// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

/* UUID : {7096bc46-8fc5-4033-8e85-f83473ce3e29} */
#define UUID                                         \
  {                                                  \
    0x7096bc46, 0x8fc5, 0x4033, {                    \
      0x8e, 0x85, 0xf8, 0x34, 0x73, 0xce, 0x3e, 0x29 \
    }                                                \
  }

NBL_APP_MANIFEST(UUID,
                 // Here config 10MB heap size.
                 NBL_APP_CONFIG_MAX_HEAP_SIZE(10 * 1024 * 1024));
