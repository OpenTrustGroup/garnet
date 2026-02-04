# Simple fuzzing test tool.

CUR_DIR = ${shell pwd}

# include common setting of makefile.
include ${CUR_DIR}/common.mk

CFLAGS += -I${SDK_HEE_HOME}/nbl_nsc/include

LDFLAGS += -L${SDK_HEE_HOME}/nbl_nsc/lib -lnbl_nsc

SRC_C += main/manifest.c main/main.c \
	main/fuz_tester.c

all: fuz_common.h

TARGET = simple-fuzzer

RUN_ARG=

.PHONY: format
format: ${SRC_C}
	clang-format -style=LLVM -i $^

include ${SDK_HOME}/make/hee-program.mk
