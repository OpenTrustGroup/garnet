# Copyright 2022 GoldenRiver Technology Co., Ltd. All rights reserved.
SDK_HOME ?= ../../../../..

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

SRC_C = client.c manifest.c

TARGET = client

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




# if coverage add targets.
.PHONY: cov lcov

COV_TAR_NAME=client_cov.tar
TA_UUID=01720e43-571b-4e81-adb1-095304cdeb40

# get TAR file from target, and extract gcda files from TA file to out folder.
get-gcda:
	${SDK_HOME}/run/coverage/nbl_get_cov_data.sh -t qemu -o out -i ${TA_UUID} ${COV_TAR_NAME}

# Calculate coverage by gcda and gcno files, and then create gcov files.
# The Place where invoke command, there should be *.c source. So cov tool can create gcov files.
# You can see whole coverage in command output.
# And see coverage state in source file of *.gcov.
cov: get-gcda
	${SDK_HOME}/run/coverage/nbl_calc_cov.sh out

# Calculate and create HTML about coverage by lcov tool.
# You can see whole coverage and source in HTML.
lcov: get-gcda
	${SDK_HOME}/run/coverage/nbl_lcov.sh out
