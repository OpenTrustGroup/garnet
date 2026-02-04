# Samples for Trusty Storage API.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./unittest -I./unittest/include

LDFLAGS +=

# Trusty API libraries must be static, and linked into exe.
EXT_LIB += ${SDK_HEE_HOME}/trusty/lib/libstorage.a ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a

SRC_C = manifest.c main.c

# Program name in "out" folder.
TARGET = test-trusty-storage

include ${SDK_HOME}/make/hee-program.mk
