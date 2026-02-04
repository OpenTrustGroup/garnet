// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

#pragma once

#define SRV_NAME "com.nebula.cache.service"

#define SHARED_MEM_SIZE (1024 * 1024)
#define ACCESS_STEP (512 * 1024)
#define STEP_COUNT ((1024 * 1024) / (512 * 1024))

enum {
  CMD_MAP_SHM = 1,
  CMD_SEND_MSG = 2,
  CMD_NOTIFY_DATA = 3,
  CMD_UNMAP_SHM = 4,
};
