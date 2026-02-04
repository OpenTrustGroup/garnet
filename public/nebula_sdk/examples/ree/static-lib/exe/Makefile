# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/ree-common.mk

# C program.
SRC_C  = main.c

CFLAGS += -I${SDK_REE_HOME}/include/trusty -I../lib

LDFLAGS += -L${SDK_REE_HOME}/lib

EXT_LIB += ../lib/out/libslib.a

# The output program name in "out" folder.
TARGET = test-slib

include ${SDK_HOME}/make/ree-program.mk
