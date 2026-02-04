// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <stdlib.h>

struct function_description random_test = {
    .funcname = "random", .funcaddr = random, .argnum = 0};
// long int random(void);
PrepareTest(random) { return &random_test; }

void srandom(unsigned int seed);
struct function_description srandom_test = {.funcname = "srandom",
                                            .funcaddr = srandom,
                                            .argnum = 1,
                                            .argname = {"seed"},
                                            .argtype = {ARG_INT}};
// void srandom(unsigned int seed);
PrepareTest(srandom) { return &srandom_test; }
