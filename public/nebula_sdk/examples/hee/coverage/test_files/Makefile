# Test for libnbl_cov in multiple source code.

# make && make run-qemu && make cov or make lcov.
# You can see result in out/.


SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = manifest.c main.c test.c lib/lib1.c lib/lib2.c

CFLAGS +=

LDFLAGS += -L${SDK_HEE_HOME}/lib

TARGET = test_files
RUN_ARG =

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

# if coverage add targets.
.PHONY: cov lcov

COV_TAR_NAME=my_coverage.tar
TA_UUID=f3573a54-4af5-4d89-a6f2-11d36aa499d5

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
