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

SRC_C = \
    service.c \
    manifest.c

TARGET = service

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



# Developer should let service exit, and then create gcda data.
# I add "exit(0)/nbl_app_stop" in "_srv_b_close_handler".
# I try to add "__llvm_gcov_flush()" to force flushing coverage data, but it's failed on compiling.

# if coverage add targets.
.PHONY: cov lcov

COV_TAR_NAME=service_cov.tar
TA_UUID=7096bc46-8fc5-4033-8e85-f83473ce3e29

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
