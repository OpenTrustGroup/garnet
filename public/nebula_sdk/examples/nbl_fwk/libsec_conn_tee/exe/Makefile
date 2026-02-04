# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = manifest.c main.c

CFLAGS += \
	-I../lib \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/trusty/include

LDFLAGS += -Wall -Werror \
	-L${SDK_HEE_HOME}/lib \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib \
	-L${SDK_HEE_HOME}/trusty/lib/ \
	-L${SDK_HEE_HOME}/boringssl/lib \
	-lnbl_fwk_hee \
	-llibc-trusty \
	-lmtee_api \
	-lcrypto

CFLAGS += -O0

EXT_LIB += ../lib/out/libsec_conn.a ${SDK_HEE_HOME}/trusty/lib/libstorage.a

# The output program name in "out" folder.
TARGET = sec_conn_test

include ${SDK_HOME}/make/hee-program.mk
