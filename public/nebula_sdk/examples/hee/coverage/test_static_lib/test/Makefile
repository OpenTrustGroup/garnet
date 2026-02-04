# Copyright 2022 GoldenRiver Technology Co., Ltd. All rights reserved.
SDK_HOME ?= ../../../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS += \
	-I../lib

LDFLAGS += -Wall -Werror \
	-L../lib/out -lcov_test

SRC_C = test.c main.c manifest.c

TARGET = test

# if coverage, add cflags and ldflags
SRC_C += cov_cfg.c

CFLAGS += --coverage -I${SDK_HEE_HOME}/trusty/include -I./ \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/nbl_nsc/include \
	-I${SDK_HEE_HOME}/nbl_cov/include

LDFLAGS += -L${SDK_HEE_HOME}/nbl_tar/lib -lnbl_tar \
	-L${SDK_HEE_HOME}/nbl_cov/lib -lnbl_cov \
	-L${SDK_HEE_HOME}/trusty/lib -llibc-trusty -lmtee_api \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee \
	-L${SDK_HEE_HOME}/nbl_nsc/lib -lnbl_nsc

include ${SDK_HOME}/make/hee-program.mk

# It must be after "include ... hee-program.mk", because the first target must be all in it.
install:
	${SDK_HOME}/run/install-file-in-qemu.sh -e out/${TARGET} -o /bin

