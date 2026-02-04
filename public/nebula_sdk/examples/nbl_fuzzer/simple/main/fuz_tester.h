// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Invoke tester of funzzing.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <trusty_ipc.h>

int fuz_start_cmd(const char* cmd_arg);
int fuz_start_fuzzing(fuz_seed_t seed,
                      uint32_t no,
                      uint32_t function_inx,
                      uint32_t run_count);
