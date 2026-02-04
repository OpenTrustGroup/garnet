# Samples for make a far running in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/nbl_attest/include \

LDFLAGS += -Wall -Werror \
	-L${SDK_HEE_HOME}/nbl_attest/lib \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib \
	-L${SDK_HEE_HOME}/trusty/lib/ \
	-lnbl_fwk_hee \
	-llibc-trusty \
	-lnbl_attest \
	-lmtee_api \

SRC_C = main.c manifest.c

# Program name in "out" folder.
TARGET = nfc_demo_ta

include ${SDK_HOME}/make/hee-program.mk
