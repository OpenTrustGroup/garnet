# Samples for exe using a shared library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = lib.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include

LDFLAGS +=

# The output program which name is "lib${TARGET}.so" is in "out" folder.
TARGET = slib

include ${SDK_HOME}/make/hee-shared-lib.mk

# install .so into the image, the "lib.manifest" can be any name.
install:
	${SDK_HOME}/host/x64/add-file-in-userbootfs lib.manifest
