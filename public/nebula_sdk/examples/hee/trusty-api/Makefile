# Samples for Trusty API in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./include -I./unittest -I./unittest/include

LDFLAGS +=

# Trusty API libraries must be static, and linked into EXE.
EXT_LIB += ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a

# Use deprecated trusty api, such as port_create. New api is _trusty_port_create.
enable_deprecated_syscalls = true

# main.c is copie from ${NEBULA}/zircon/trusty/app/gr-ipc-unittest/main/main.c, and edit it.
SRC_C = manifest.c main.c \
	unittest/all-tests.cpp \
	unittest/crash-handler.cpp \
	unittest/crash-list.cpp \
	unittest/unittest.cpp

# Program name in "out" folder.
TARGET = test-trusty-api

SD_PRIVILEGE_FILE = sd.privilege

include ${SDK_HOME}/make/hee-program.mk
