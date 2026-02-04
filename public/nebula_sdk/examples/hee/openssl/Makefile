# Samples for openssl.

SDK_HOME ?= ../../..

# Disable syscalls w/o "_trusty_" prefix to prevent naming conflicts.
enable_deprecated_syscalls = false

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
		-I${SDK_HEE_HOME}/openssl/include

CFLAGS +=

LDFLAGS += -L${SDK_HEE_HOME}/openssl/lib -lopenssl_crypto

EXT_LIB +=

SRC_C = manifest.c main.c

# Program name in "out" folder.
TARGET = test-openssl

include ${SDK_HOME}/make/hee-program.mk
