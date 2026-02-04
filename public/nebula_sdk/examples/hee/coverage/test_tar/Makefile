# Test for libtar
# "make", "make run-qemu", and you can see a tar file in "/var/tmp/nss" in Qemu/REE.

SDK_HOME ?= ../../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

# C program.
SRC_C  = manifest.c main.c

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \
	-I${SDK_HEE_HOME}/nbl_nsc/include \
	-I${SDK_HEE_HOME}/nbl_tar/include

LDFLAGS += -L${SDK_HEE_HOME}/lib \
	-L${SDK_HEE_HOME}/trusty/lib -llibc-trusty -lmtee_api \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee \
	-L${SDK_HEE_HOME}/nbl_nsc/lib -lnbl_nsc \
	-L${SDK_HEE_HOME}/nbl_tar/lib -lnbl_tar \


TARGET = test-tar

include ${SDK_HOME}/make/hee-program.mk
