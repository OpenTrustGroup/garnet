# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = sec_srv.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/boringssl/include \
	-I./

# static library doesn't need LDFLAGS.
#LDFLAGS +=

# The output program whhich name is "lib${TARGET}.a" is in "out" folder.
TARGET = sec_conn

include ${SDK_HOME}/make/hee-static-lib.mk
