# Copyright 2022 GoldenRiver Technology Co., Ltd. All rights reserved.
SDK_HOME ?= ../../../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS +=

LDFLAGS += -Wall -Werror

SRC_C = lib.c

TARGET = cov_test

# if coverage, add cflags and ldflags
CFLAGS += --coverage

include ${SDK_HOME}/make/hee-static-lib.mk


#### HERE, You want to calculate coverage about static library,
#### so, you add cov and lcov target in static library makefile,
#### and add compile flags/library in EXE makefile to build.


# if coverage add targets.
.PHONY: cov lcov

COV_TAR_NAME=test_cov.tar
# Tester's UUID
TA_UUID=569f5b5d-ba30-4f38-bec9-eea24cb982f2

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
