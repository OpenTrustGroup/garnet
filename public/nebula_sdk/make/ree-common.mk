# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make for CA running in Linux of QEMU.
# 1, Use clang to compile, and but link the library in linaro gcc toolchain.

SHELL=/bin/bash

# check SDK_HOME
ifeq ($(origin SDK_HOME), undefined)
  $(error "Set the SDK_HOME to sdk root directory.")
endif

# check TOOL_CHAIN_HOME
exist = false

# If the toolchain is NOT defined, then the script will try to find the path of toolchain.
ifeq ($(origin TOOL_CHAIN_HOME), undefined)
  ifeq ($(origin FUCHSIA_DIR), undefined) # If used by third-paarty developers, and toolchain is in SDK.
    TOOL_CHAIN_HOME = $(SDK_HOME)/toolchains/clang
    TOOL_CHAIN_GCC_HOME = $(SDK_HOME)/toolchains/linaro-gcc
  else # If compiled in Fuchsia Env.
    TOOL_CHAIN_HOME = ${FUCHSIA_DIR}/buildtools/linux-x64/clang
    TOOL_CHAIN_GCC_HOME = ${FUCHSIA_DIR}/prebuilts/gcc-linaro-6.2.1-2016.11
  endif
endif

exist = $(shell if [ -d ${TOOL_CHAIN_HOME} ]; then echo "true"; else echo "false"; fi;)
ifeq (${exist}, true)
  $(info Find toolchain in ${TOOL_CHAIN_HOME})
else
  $(error "Set the TOOL_CHAIN_HOME to toolchain root directory.")
endif

exist = $(shell if [ -d ${TOOL_CHAIN_GCC_HOME} ]; then echo "true"; else echo "false"; fi;)
ifeq (${exist}, true)
  $(info Find gcc toolchain in ${TOOL_CHAIN_GCC_HOME})
else
  $(error "Set the TOOL_CHAIN_GCC_HOME to toolchain root directory.")
endif

#define SDK
SDK_REE_HOME=${SDK_HOME}/ree

###########################################################
# define the tools in toolchain.
TOOL_CHAIN_PREFIX = ${TOOL_CHAIN_HOME}/bin
CC   =${TOOL_CHAIN_PREFIX}/clang
CPP  =${TOOL_CHAIN_PREFIX}/clang++
LD   =${TOOL_CHAIN_PREFIX}/ld.lld
AR   =${TOOL_CHAIN_PREFIX}/llvm-ar
# clang use objcopy to strip ELF. llvm-objcopy <from> <to>.
STRIP=${TOOL_CHAIN_PREFIX}/llvm-objcopy --strip-all-gnu

# Flags for comiling C and C++

CFLAGS = --target=aarch64-linux-gnu \
  --gcc-toolchain=$(TOOL_CHAIN_GCC_HOME) \
  --sysroot=$(TOOL_CHAIN_GCC_HOME)/aarch64-linux-gnu/libc \
  -I${SDK_REE_HOME}/include

# Flags for comiling C
ONLY_CFLAGS =

# Flags for compiling C++
ONLY_CPPFLAGS =

# Flags for linking.
# - use the libc.so in toolchains.
LDFLAGS= --target=aarch64-linux-gnu \
  --gcc-toolchain=$(TOOL_CHAIN_GCC_HOME) \
  --sysroot=$(TOOL_CHAIN_GCC_HOME)/aarch64-linux-gnu/libc \
  -L${SDK_REE_HOME}/lib -lnbl_trusty

############################################################
# Compile rules.

out/%.c.o : %.c
	mkdir -p `dirname $@`
	$(CC)  $(CFLAGS) $(ONLY_CFLAGS)   -c $< -o out/$<.o

out/%.cc.o : %.cc
	mkdir -p `dirname $@`
	$(CPP) $(CFLAGS) $(ONLY_CPPFLAGS) -c $< -o out/$<.o

out/%.cpp.o : %.cpp
	mkdir -p `dirname $@`
	$(CPP) $(CFLAGS) $(ONLY_CPPFLAGS) -c $< -o out/$<.o

out/%.cxx.o : %.cxx
	mkdir -p `dirname $@`
	$(CPP) $(CFLAGS) $(ONLY_CPPFLAGS) -c $< -o out/$<.o
