# Copyright 2022 GoldenRiver Technologies Co., Ltd. All rights reserved.

# Example for google benchmark.
# If terminal doesn't support color, set benchmark_color to false.
#   such as: [0;32mBM_demo/iterations:1000 [m[0;33m      1162 ns          989 ns   [m[0;36m      1000[m
# If terminal support color, set nothing to RUN_ARG.

SDK_HOME ?= ../..

include ${SDK_HOME}/make/hee-common.mk

# At first, benchmark static lib shall be built.
benchmark: 
	make -C ${SDK_HEE_HOME}/benchmark build
all: benchmark

CFLAGS += -I${SDK_HEE_HOME}/benchmark/include

LDFLAGS +=

EXT_LIB += ${SDK_HEE_HOME}/benchmark/lib/libbenchmark.a

SRC_C = manifest.cc demo.cc

TARGET = bench

# show log without color.
RUN_ARG="--benchmark_color=false"

include ${SDK_HOME}/make/hee-program.mk

