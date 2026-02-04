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

SDK_HOME ?= ../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./unittest -I./unittest/include

LDFLAGS +=

EXT_LIB += ${SDK_HEE_HOME}/trusty/lib/libstorage.a ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a

# The functions are same with storage-mng, expect UUID.
SRC_C = main.c manifest.c

TARGET = storage-mng2

include ${SDK_HOME}/make/hee-program.mk
