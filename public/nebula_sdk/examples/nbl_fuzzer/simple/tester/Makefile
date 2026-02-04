# Simple fuzzing tester.

CUR_DIR = ${shell pwd}

# include common setting of makefile.
include ${CUR_DIR}/common.mk

SRC_C += tester/manifest.c tester/main.c

all: fuz_common.h

TARGET = simple-tester

SD_PRIVILEGE_FILE = sd.privilege

FAR_FILES += \
  assets/gr1.png=${CUR_DIR}/assets/gr1.png, \
  assets/back.png=${CUR_DIR}/assets/back.png, \
  assets/back2.png=${CUR_DIR}/assets/back2.png, \
  assets/layout.json=${CUR_DIR}/assets/layout.json, \
  assets/layout.css=${CUR_DIR}/assets/layout.css, \
  assets/DroidSansFallbackFull.ttf=${CUR_DIR}/assets/DroidSansFallbackFull.ttf, \
  assets/test.json=${CUR_DIR}/assets/test.json

RUN_ARG=

.PHONY: format
format: ${SRC_C}
	clang-format -style=LLVM -i $^

#include ${SDK_HOME}/make/hee-program.mk
include ${SDK_HOME}/make/hee-tui.mk
