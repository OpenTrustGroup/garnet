# Samples for exe using a shared library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/ree-common.mk

# C program.
SRC_C  = lib.c

CFLAGS += -I${SDK_REE_HOME}/include/trusty

LDFLAGS +=

# The output program which name is "lib${TARGET}.so" is in "out" folder.
TARGET = slib

include ${SDK_HOME}/make/ree-shared-lib.mk

# install the .so into the "/lib" of Linux.
install:
	${SDK_HOME}/run/install-file-in-qemu.sh -e out/lib${TARGET}.so -o /lib
