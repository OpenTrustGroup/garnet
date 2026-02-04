# Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved

# Test SM2/3/4
# refer to "regression_4000.c" in OPTEE.
# 1. folder "script" will create a SM2 ECC keypair including public and private key by openssl,
#	python script will convert "der" to c source.
#	Users also can use this python script to convert my "der" to c source.
# 2. folder "openssl" will create a SM2 ECC keypair including public and private key by API openssl.
#	And then print key data as c source.
# Users can import above c source into source as input of sm-lite sm2 functions.

SDK_HOME ?= ../..

include ${SDK_HOME}/make/hee-common.mk

SRC_C  = manifest.c main.c \
	test_sm4.c \
	test_sm3.c \
	test_ecc_key.c \
	test_sm2.c \
	test_sm2_derive.c \
	test_sm2_derive2.c \
	unittest.c \

CFLAGS +=  -I./ -I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/sm-lite/include

LDFLAGS += -L${SDK_HEE_HOME}/lib

EXT_LIB += ${SDK_HEE_HOME}/sm-lite/lib/libsm-lite.a

TARGET = test-sm234

include ${SDK_HOME}/make/hee-program.mk
