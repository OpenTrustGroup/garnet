# Samples for building a simple driver.

SDK_HOME ?= ../../../

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = touch_drv.c

# The output program which name is "lib${TARGET}.so" is in "out" folder.
TARGET = touch.default

include ${SDK_HOME}/make/hee-tui-driver.mk
