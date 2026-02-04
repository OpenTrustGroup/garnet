# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# Sample for nbl_fwk in REE.

SDK_HOME ?= ../../..

# include the common variables.
include ${SDK_HOME}/make/ree-common.mk

# define the options about compiling.
SRC_C = unitest.c

CFLAGS += \
	-I${SDK_REE_HOME}/include/trusty \
	-I${SDK_REE_HOME}/include/nbl_fwk_ree \

LDFLAGS += -lpthread -Wall -Werror \
	-L${SDK_REE_HOME}/lib -lnbl_trusty -lnbl_fwk_ree -lnbl_loader_ree

TARGET = test-nbl_fwk
RUN_ARG =

include ${SDK_HOME}/make/ree-program.mk
