# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = manifest.c main.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I../lib

LDFLAGS += -L${SDK_HEE_HOME}/lib -L../lib/out -lslib

# The output program name in "out" folder.
TARGET = test-slib

include ${SDK_HOME}/make/hee-program.mk
