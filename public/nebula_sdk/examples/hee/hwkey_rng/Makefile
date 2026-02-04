# Samples for Hwkey and rng.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include
# trusty app uses its own "sys/types.h" in this folder.
CFLAGS += -I${SDK_HEE_HOME}/trusty/include/trusty

# librng.a needs AES functions in boringssl.
LDFLAGS += -L${SDK_HEE_HOME}/trusty/lib -llibc-trusty -lhwkey -lrng \
	-L${SDK_HEE_HOME}/boringssl/lib -lcrypto

SRC_C = manifest.c 	main.c

# Program name in "out" folder.
TARGET = test-hwkey_rng

include ${SDK_HOME}/make/hee-program.mk
