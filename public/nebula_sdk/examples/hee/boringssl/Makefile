# Samples for boringssl.

SDK_HOME ?= ../../..

# Disable syscalls w/o "_trusty_" prefix to prevent naming conflicts.
enable_deprecated_syscalls = false

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
		-I${SDK_HEE_HOME}/boringssl/include \

CFLAGS +=

CFLAGS +=

LDFLAGS += -L${SDK_HEE_HOME}/boringssl/lib -lcrypto

EXT_LIB +=

SRC_C = manifest.c \
	main.c \

# Program name in "out" folder.
TARGET = boringssl-example

include ${SDK_HOME}/make/hee-program.mk
