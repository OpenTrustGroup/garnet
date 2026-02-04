# Just a example for running in TEE.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = manifest.c main.c lib/lib.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include -I./lib

LDFLAGS += -L${SDK_HEE_HOME}/lib

# The output program name in "out" folder.
TARGET = test-libc
# - The $RUN_ARG will be used on running this application.
# - Users can run command "RUN_ARG=123 make run" for trying,
#   and it will overwrite here setting.
RUN_ARG =

include ${SDK_HOME}/make/hee-program.mk
