// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

/* UUID : {01720e43-571b-4e81-adb1-095304cdeb40} */
#define UUID                                         \
  {                                                  \
    0x01720e43, 0x571b, 0x4e81, {                    \
      0xad, 0xb1, 0x09, 0x53, 0x04, 0xcd, 0xeb, 0x40 \
    }                                                \
  }

NBL_APP_MANIFEST(UUID,
                 // Here config 10MB heap size.
                 NBL_APP_CONFIG_MAX_HEAP_SIZE(10 * 1024 * 1024));
