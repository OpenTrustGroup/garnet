// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

/* App UUID:  {a47f667c-842b-4f11-b22b-0a32e9ac2150} */
#define UUID                                                                   \
  {                                                                            \
    0xa47f667c, 0x842b, 0x4f11, {                                              \
      0xb2, 0x2b, 0x0a, 0x32, 0xe9, 0xac, 0x21, 0x50                           \
    }                                                                          \
  }

NBL_APP_MANIFEST(UUID, TRUSTY_APP_CONFIG_MAP_MEM(1, 0x70000000, 0x2000),
                 NBL_APP_CONFIG_IRQ(100),
                 NBL_APP_CONFIG_MAX_HEAP_SIZE(30 * 1024 * 1024));
