# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
SDK_HOME ?= ../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS += \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/trusty/include

LDFLAGS += -Wall -Werror \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib \
    -L${SDK_HEE_HOME}/trusty/lib/ \
	-lnbl_fwk_hee \
    -llibc-trusty \
    -lmtee_api

SRC_C = \
    client.c \
    manifest.c

TARGET = simple_fwk_ha_client

include ${SDK_HOME}/make/hee-program.mk

# It must be after "include ... hee-program.mk", because the first target must be all in it.
install:
	${SDK_HOME}/run/install-file-in-qemu.sh -e out/${TARGET} -o /bin
