# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
SDK_HOME ?= ../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -O0 \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/trusty/include

LDFLAGS += -Wall -Werror \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib \
    -L${SDK_HEE_HOME}/trusty/lib/ \
	-lnbl_fwk_hee \
    -llibc-trusty \
    -lmtee_api

SRC_C = \
    main.c \
    manifest.c

TARGET = cache_ta1

include ${SDK_HOME}/make/hee-program.mk
