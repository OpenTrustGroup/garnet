# Samples for "create a new process" in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = manifest.c main.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include

LDFLAGS += -L${SDK_HEE_HOME}/lib

# The output program name in "out" folder.
TARGET = test-new_process
# - The $RUN_ARG will be used on running this application.
# - Users can run command "RUN_ARG=123 make run" for trying,
#   and it will overwrite here setting.
RUN_ARG =

ifeq ($(SD_ALLOW), 0)
  SD_PRIVILEGE_FILE = sd.privilege.forbid
else ifeq ($(SD_ALLOW), 1)
  SD_PRIVILEGE_FILE = sd.privilege.allow
endif

include ${SDK_HOME}/make/hee-program.mk
