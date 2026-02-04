// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

// uuid: 94e8bd48-bc3f-4461-a413-5993724d17f0
#define UUID                                         \
  {                                                  \
    0x94e8bd48, 0xbc3f, 0x4461, {                    \
      0xa4, 0x13, 0x59, 0x93, 0x72, 0x4d, 0x17, 0xf0 \
    }                                                \
  }

NBL_APP_MANIFEST(UUID, NBL_APP_CONFIG_MAX_HEAP_SIZE(4 * 1024 * 1024), );
