# Samples for make a far running in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include

LDFLAGS += -lm

EXT_LIB +=

SRC_C = main.c manifest.c

# Program name in "out" folder.
TARGET = test-far

FAR_FILES += \
	data/test1.dat=data/test1.dat, \
	data/test2.dat=data/test2.dat

include ${SDK_HOME}/make/hee-far.mk
