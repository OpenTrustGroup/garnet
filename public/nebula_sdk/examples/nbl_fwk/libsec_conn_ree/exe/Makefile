# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/ree-common.mk

# C program.
SRC_C  = main.c

CFLAGS += \
	-I../lib \
	-I${SDK_REE_HOME}/include/nbl_fwk_ree \
	-I${SDK_REE_HOME}/include/trusty \

LDFLAGS += -lpthread -Wall -Werror \
	-L${SDK_REE_HOME}/lib \
	-lnbl_fwk_ree \
	-lnbl_trusty \
	-lcrypto \
	-ldl

EXT_LIB += ../lib/out/libsec_conn_ree.a

# The output program name in "out" folder.
TARGET = test-slib

include ${SDK_HOME}/make/ree-program.mk
