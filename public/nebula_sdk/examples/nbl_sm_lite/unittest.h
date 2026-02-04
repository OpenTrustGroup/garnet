// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ARRAY(a) a, ARRAY_SIZE(a)
#define NULL_ARRAY(a) NULL, 0

#define TLOGI(fmt, ...) \
  printf("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define CHECK_BUF_EQ(expected, actual, len, msg)                   \
  {                                                                \
    for (__typeof__(len) _i = 0; _i < len; _i++) {                 \
      __typeof__(actual[0]) _e = expected[_i];                     \
      __typeof__(actual[0]) _a = actual[_i];                       \
      if (_e != _a) {                                              \
        printf("[%s:%d] %s: expected " #expected                   \
               "[%zu] (%d), "                                      \
               "actual " #actual "[%zu] (%d)\n",                   \
               __func__, __LINE__, msg, _i, (int)_e, _i, (int)_a); \
        goto failed;                                               \
      }                                                            \
    }                                                              \
  }
