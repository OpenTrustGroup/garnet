# Copyright 2020 GoldenRiver Technology Co., Ltd.
# Example for nbl time API.

SDK_HOME ?= ../../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include

LDFLAGS += -L${SDK_HEE_HOME}/trusty/lib/ -llibc-trusty \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee -lmtee_api

SRC_C = ../time.c manifest.c

# if set Ta persistent time.
#RUN_ARG =-s 10.10
TARGET = time1

include ${SDK_HOME}/make/hee-program.mk

clean: my_clean
my_clean:
	-rm *.o
