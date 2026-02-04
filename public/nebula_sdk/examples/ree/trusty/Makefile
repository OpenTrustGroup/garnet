# Sample for tipc in REE.

SDK_HOME ?= ../../..

# include the common variables.
include ${SDK_HOME}/make/ree-common.mk

# define the options about compiling.
## tipc_test.c is copied from "//third_party/ree-trusty/libtrusty/tipc-test/tipc_test.c" and delete the tests for loader.
SRC_C = tipc_test.c
CFLAGS +=
LDFLAGS += -lpthread

TARGET = test-tipc
RUN_ARG = -t ta2ta-ipc

include ${SDK_HOME}/make/ree-program.mk
