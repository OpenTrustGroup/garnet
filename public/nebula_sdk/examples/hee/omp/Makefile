# Samples for OpenMP.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/omp/include \
	-fopenmp

LDFLAGS +=

SRC_C = manifest.c main.c

EXT_LIB += ${SDK_HEE_HOME}/omp/lib/libiomp5.a ${SDK_HEE_HOME}/omp/lib/libomp.a

# Program name in "out" folder.
TARGET = test-omp

include ${SDK_HOME}/make/hee-program.mk
