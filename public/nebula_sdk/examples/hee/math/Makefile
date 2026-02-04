# Samples for Math of libc.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include

LDFLAGS += -lm

EXT_LIB +=

SRC_C = manifest.c main.c

# Program name in "out" folder.
TARGET = test-math

include ${SDK_HOME}/make/hee-program.mk
