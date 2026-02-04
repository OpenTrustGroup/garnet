// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include "nbl_fwk/ntime.h"
#include "time_test.h"
#include <fuz_common.h>
#include <zircon/syscalls.h>

struct function_description nbl_time_get_ree_test = {
    .funcname = "nbl_time_get_ree",
    .funcaddr = nbl_time_get_ree,
    .argnum = 1,
    .argname = {"time"},
    .argtype = {ARG_NORMAL_BUF}};
PrepareTest(nbl_time_get_ree) { return &nbl_time_get_ree_test; };

struct function_description nbl_time_get_system_test = {
    .funcname = "nbl_time_get_system",
    .funcaddr = nbl_time_get_system,
    .argnum = 1,
    .argname = {"time"},
    .argtype = {ARG_NORMAL_BUF}};
PrepareTest(nbl_time_get_system) { return &nbl_time_get_system_test; };

struct function_description nbl_time_set_ta_persistent_test = {
    .funcname = "nbl_time_set_ta_persistent",
    .funcaddr = nbl_time_set_ta_persistent,
    .argnum = 1,
    .argname = {"time"},
    .argtype = {ARG_NORMAL_BUF}};
PrepareTest(nbl_time_set_ta_persistent) {
  return &nbl_time_set_ta_persistent_test;
};

struct function_description nbl_time_get_ta_persistent_test = {
    .funcname = "nbl_time_get_ta_persistent",
    .funcaddr = nbl_time_get_ta_persistent,
    .argnum = 1,
    .argname = {"time"},
    .argtype = {ARG_NORMAL_BUF}};
PrepareTest(nbl_time_get_ta_persistent) {
  return &nbl_time_get_ta_persistent_test;
};

static uint32_t ZX_CLOCK_ID_FLAGS[] = {ZX_CLOCK_MONOTONIC, ZX_CLOCK_UTC,
                                       ZX_CLOCK_THREAD};
struct function_description zx_clock_get_test = {
    .funcname = "zx_clock_get",
    .funcaddr = zx_clock_get,
    .argnum = 1,
    .argname = {"clock_id"},
    .argtype = {ARG_INT},
    .argselcnt[0] = ARRAY_SIZE(ZX_CLOCK_ID_FLAGS),
    .argselect[0] = ZX_CLOCK_ID_FLAGS};
PrepareTest(zx_clock_get) { return &zx_clock_get_test; };

struct function_description zx_clock_adjust_test_test = {
    .funcname = "zx_clock_adjust_test",
    .funcaddr = zx_clock_adjust_test,
    .argnum = 2,
    .argname = {"clock_id", "offset"},
    .argtype = {ARG_INT, ARG_INT64}};
PrepareTest(zx_clock_adjust_test) { return &zx_clock_adjust_test_test; };
