// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_app_manifest.h>
#include <stddef.h>

/* App UUID:  {ab742471-d6e6-4806-85f6-0555b024f4da} */
/* same as HWCRYPTO unittest App UUID */
#define FUZZING_TEST_APP_UUID                                                  \
  {                                                                            \
    0xab742471, 0xd6e6, 0x4806, {                                              \
      0x85, 0xf6, 0x05, 0x55, 0xb0, 0x24, 0xf4, 0xda                           \
    }                                                                          \
  }

NBL_APP_MANIFEST(FUZZING_TEST_APP_UUID,
                 TRUSTY_APP_CONFIG_MAP_MEM(1, 0x70000000, 0x2000),
                 NBL_APP_CONFIG_IRQ(100),
                 NBL_APP_CONFIG_MAX_HEAP_SIZE(40 * 1024 * 1024),
                 NBL_APP_TUI(1));
