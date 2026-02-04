# Samples for make a far running in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/nbl_nsc/include -I${SDK_HEE_HOME}/trusty/include -I${SDK_HEE_HOME}/nbl_fwk_hee/include -Iinclude \
		-I${SDK_HEE_HOME}/boringssl/include \

LDFLAGS += -L${SDK_HEE_HOME}/boringssl/lib -lcrypto -L${SDK_HEE_HOME}/nbl_nsc/lib -lnbl_nsc \
		-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee \

# Trusty API libraries must be static, and linked into EXE.
EXT_LIB += ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a \
			${SDK_HEE_HOME}/trusty/lib/libmtee_api.a \

SRC_C = main.c manifest.c

TARGET = nbl_random

include ${SDK_HOME}/make/hee-program.mk
