# Samples for pthread.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
		-I./

LDFLAGS += -lpthread -L./ -lunittest -lpretty

EXT_LIB +=

SRC_C = manifest.c pthread.cpp

# Program name in "out" folder.
TARGET = test-pthread

include ${SDK_HOME}/make/hee-program.mk
