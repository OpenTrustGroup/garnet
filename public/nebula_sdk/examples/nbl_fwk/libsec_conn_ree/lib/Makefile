# Samples for exe using a static library.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/ree-common.mk

# C program.
SRC_C  = sec_client.c

CFLAGS += -I./

# static library doesn't need LDFLAGS.
#LDFLAGS +=

# The output program which name is "lib${TARGET}.a" is in "out" folder.
TARGET = sec_conn_ree

include ${SDK_HOME}/make/ree-static-lib.mk
