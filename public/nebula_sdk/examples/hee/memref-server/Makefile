# Samples for memref in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./include -I./unittest -I./unittest/include

LDFLAGS +=

# Trusty API libraries must be static, and linked into EXE.
EXT_LIB += ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a

# Use deprecated trusty api, such as port_create. New api is _trusty_port_create.
enable_deprecated_syscalls = true

SRC_C = manifest.c main.c \

# Program name in "out" folder.
TARGET = memref_sample_server

include ${SDK_HOME}/make/hee-program.mk
