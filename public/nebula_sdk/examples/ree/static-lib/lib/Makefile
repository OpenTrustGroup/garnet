# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/ree-common.mk

# C program.
SRC_C  = lib.c

CFLAGS += -I${SDK_REE_HOME}/include/trusty

# static library doesn't need LDFLAGS.
#LDFLAGS +=

# The output program which name is "lib${TARGET}.a" is in "out" folder.
TARGET = slib

include ${SDK_HOME}/make/ree-static-lib.mk
