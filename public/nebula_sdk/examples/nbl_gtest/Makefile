# Copyright 2018 GoldenRiver Technologies Co., Ltd. All rights reserved.
#
# The present software is the confidential and proprietary information of
# GoldenRiver Technologies Co., Ltd. You shall not disclose the present
# software and shall use it only in accordance with the terms of the license
# agreement you entered into with GoldenRiver Technologies Co., Ltd.
# This software may be subject to export or import laws in certain countries.

# Application about Storage for Test.
# - create the file and simple content.
# - list the files in secure storage.

SDK_HOME ?= ../..

include ${SDK_HOME}/make/hee-common.mk

# At first, googletest static lib shall be built.
gtest:
	make -C ${SDK_HEE_HOME}/googletest build
all: gtest

CFLAGS += -I${SDK_HEE_HOME}/googletest/include

LDFLAGS +=

EXT_LIB +=  ${SDK_HEE_HOME}/googletest/lib/libgtest.a 

SRC_C = manifest.cc footest.cc

TARGET = footest

RUN_ARG=

include ${SDK_HOME}/make/hee-program.mk

