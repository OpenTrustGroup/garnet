// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <zircon/syscalls.h>

// zx_status_t zx_cprng_draw(void* buffer, size_t len, size_t* actual);

struct function_description zx_cprng_draw_test = {
    .funcname = "zx_cprng_draw",
    .funcaddr = zx_cprng_draw,
    .argnum = 3,
    .argname = {"buffer", "len", "actual"},
    .argtype = {ARG_ADDRESS, ARG_LEN, ARG_ADDRESS},
    .argmax[2] = sizeof(size_t),
    .argdep[1] = 0 + ARG_DEPEND_MIN,
};

PrepareTest(zx_cprng_draw) { return &zx_cprng_draw_test; }
