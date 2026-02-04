# Samples for STL.

SDK_HOME ?= ../../..

# Disable syscalls w/o "_trusty_" prefix to prevent naming conflicts.
enable_deprecated_syscalls = false

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include

LDFLAGS +=

EXT_LIB +=

SRC_C = manifest.c main.cc

# Program name in "out" folder.
TARGET = test-stl

include ${SDK_HOME}/make/hee-program.mk
