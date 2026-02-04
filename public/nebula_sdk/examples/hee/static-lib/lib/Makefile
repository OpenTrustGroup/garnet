# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = lib.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include

# static library doesn't need LDFLAGS.
#LDFLAGS +=

# The output program whhich name is "lib${TARGET}.a" is in "out" folder.
TARGET = slib

include ${SDK_HOME}/make/hee-static-lib.mk
