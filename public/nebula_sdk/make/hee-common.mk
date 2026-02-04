# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Common setting for Makefile.

SHELL=/bin/bash

# set V=1 in the environment if you want to see the full command line of every command
ifeq ($(V),1)
NOECHO :=
else
NOECHO ?= @
endif

# Common macro and function.
TOBOOL = $(if $(filter-out 0 false,$1),true,false)

# Check SDK_HOME
ifeq ($(origin SDK_HOME), undefined)
  $(error "Set the SDK_HOME to sdk root directory.")
endif

# Check TOOL_CHAIN_HOME (optional).
# - If the toolchain path is NOT defined, the script will try to find the default path of toolchain.
exist = false

ifeq ($(origin TOOL_CHAIN_HOME), undefined) # If user doesn't set this var, find the default path by env.
  ifeq ($(origin FUCHSIA_DIR), undefined) # If used by third-party developers, the toolchain should be in SDK.
    TOOL_CHAIN_HOME = $(SDK_HOME)/toolchains/clang
  else  # If compiled in Fuchsia Env, the toolchain should be in buildtools.
    TOOL_CHAIN_HOME = ${FUCHSIA_DIR}/buildtools/linux-x64/clang
  endif
endif

# Check the toolchain exists.
exist = $(shell if [ -d ${TOOL_CHAIN_HOME} ]; then echo "true"; else echo "false"; fi;)
ifeq (${exist}, true)
  $(info Find toolchain in ${TOOL_CHAIN_HOME})
else
  $(error "Don't find the toolchain in \"${TOOL_CHAIN_HOME}\", so please set the TOOL_CHAIN_HOME to toolchain root directory.")
endif

# Default disable support of deprecated syscalls: port_create/close...
# Better to use new syscalls: _trusty_port_create/_trusty_close...
enable_deprecated_syscalls ?= false

LOCAL_OBJS_DIR ?= out

# Define path about HEE in SDK.
SDK_HEE_HOME=${SDK_HOME}/hee

# Set the default SD_PRIVILEGE_FILE
SD_PRIVILEGE_FILE=${SDK_HOME}/examples/sd.privilege

############################################################

FAR  =${SDK_HOME}/host/x64/far
CREATE_ARCHIVE_MANIFEST=${SDK_HOME}/host/x64/create_archive_manifest

TOOL_CHAIN_PREFIX = ${TOOL_CHAIN_HOME}/bin
CC   =${TOOL_CHAIN_PREFIX}/clang
CPP  =${TOOL_CHAIN_PREFIX}/clang++
LD   =${TOOL_CHAIN_PREFIX}/ld.lld
AR   =${TOOL_CHAIN_PREFIX}/llvm-ar
# clang use objcopy to strip ELF. llvm-objcopy <from> <to>.
STRIP=${TOOL_CHAIN_PREFIX}/llvm-objcopy --strip-all-gnu

# Compile flag only for C.
# --sysroot=<dir> : It will search headers and libs in <dir>/usr/include and <dir>/usr/lib.
# --target=aarch64-fuchsia : Clang will read the header and lib from this target folder.
# -no-canonical-prefixes : don't expand any symbolic links.
ONLY_CFLAGS = --sysroot=${SDK_HEE_HOME} \
	--target=aarch64-fuchsia -no-canonical-prefixes -g

# Compile Flag only for C++.
ONLY_CPPFLAGS = --sysroot=${SDK_HEE_HOME} \
	--target=aarch64-fuchsia -no-canonical-prefixes -g

ifeq ($(ENABLE_CPP_EXCEPTION_FEATURE),1)
$(info c++ exception feature is enabled)
else
ONLY_CPPFLAGS += -fno-exceptions
endif

ifeq ($(ENABLE_CPP_RTTI_FEATURE),1)
$(info c++ rtti feature is enabled)
else
ONLY_CPPFLAGS += -fno-rtti
endif


CFLAGS += \
        -I${SDK_HEE_HOME}/nbl_fwk_hee/include \
        -I${SDK_HEE_HOME}/nbl_nsc/include \
        -I${SDK_HEE_HOME}/nbl_loader/include \
        -I${SDK_HEE_HOME}/fsl/include \
        -I${SDK_HEE_HOME}/fxl/include \
        -I${SDK_HEE_HOME}/nebula-cpp/include \
        -I${SDK_HEE_HOME}/pretty/include \
        -I${SDK_HEE_HOME}/trusty/include \
        -I${SDK_HEE_HOME}/zircon/include \
        -I${SDK_HEE_HOME}/zlib/include \
        -I${SDK_HEE_HOME}/component/include \
        -I${SDK_HEE_HOME}/app/include \
        -I${SDK_HEE_HOME}/fdio/include \
        -I${SDK_HEE_HOME}/mem/include \
        -I${SDK_HEE_HOME}/fidl/include \
        -I${SDK_HEE_HOME}/fidl_cpp/include \
        -I${SDK_HEE_HOME}/fs/include \
        -I${SDK_HEE_HOME}/svc/include \
        -I${SDK_HEE_HOME}/async/include \
        -I${SDK_HEE_HOME}/async-cpp/include \
        -I${SDK_HEE_HOME}/async-default/include \
        -I${SDK_HEE_HOME}/async-loop/include \
        -I${SDK_HEE_HOME}/async-loop-cpp/include \

# Link flag. (Deprecated)
# --icf=all : enable identical code folding.
# -lfdio -lzircon : all programs needs the libraries.
# -z max-page-size : is for linker, but gcc will transfer the "-z" to linker without the "-Wl,". It will reduce the size of elf.
#LDFLAGS = --sysroot=${SDK_HEE_HOME} \
	--target=aarch64-fuchsia -Wl,--icf=all \
	-z max-page-size=4096 -g \
	-L${SDK_HEE_HOME}/lib -lzircon \
	-L${SDK_HEE_HOME}/fdio/lib -lfdio \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee

# New link flag to support TA lifecycle management
LDFLAGS += \
	--sysroot=${SDK_HEE_HOME} \
	--target=aarch64-fuchsia \
	-z max-page-size=4096 -g \
	-Wall -Werror \
	-L${SDK_HEE_HOME}/lib -lzircon \
	-L${SDK_HEE_HOME}/nbl_nsc/lib -lnbl_nsc \
	-L${SDK_HEE_HOME}/nbl_loader/lib -lfidl \
	-L${SDK_HEE_HOME}/fsl/lib -lfsl \
	-L${SDK_HEE_HOME}/fxl/lib -lfxl -lfxl_logging \
	-L${SDK_HEE_HOME}/nebula-cpp/lib -lnebula-base -lnebula-ipc -lnebula-rpc \
	-L${SDK_HEE_HOME}/pretty/lib -lpretty \
	-L${SDK_HEE_HOME}/trusty/lib -llibc-trusty -lstorage -lrng -lhwkey -lmtee_api -lkeymaster -ltipc \
	-L${SDK_HEE_HOME}/zircon/lib -lfbl -lzx -ltrace -ltrace-engine \
	-L${SDK_HEE_HOME}/zlib/lib -lz \
	-L${SDK_HEE_HOME}/component/lib -lfidl \
	-L${SDK_HEE_HOME}/app/lib -lcpp \
	-L${SDK_HEE_HOME}/fdio/lib -lfdio \
	-L${SDK_HEE_HOME}/mem/lib -lfidl \
	-L${SDK_HEE_HOME}/fidl/lib -lfidl \
	-L${SDK_HEE_HOME}/fidl_cpp/lib -lcpp \
	-L${SDK_HEE_HOME}/fs/lib -lfs \
	-L${SDK_HEE_HOME}/svc/lib -lcpp \
	-L${SDK_HEE_HOME}/async/lib -lasync \
	-L${SDK_HEE_HOME}/async-cpp/lib -lasync.cpp \
	-L${SDK_HEE_HOME}/async-default/lib -lasync.default \
	-L${SDK_HEE_HOME}/async-loop/lib -lasync-loop \
	-L${SDK_HEE_HOME}/async-loop-cpp/lib -lasync-loop.cpp \
	-Wl,-S \
	-no-canonical-prefixes \
	-Wl,--icf=all \
	-Wl,--no-undefined

############################################################
# Compile rules.

.DEFAULT_GOAL := all

dump_trace:
	@${SDK_HOME}/run/dump_trace.sh

${LOCAL_OBJS_DIR}/%.c.o : %.c
	$(NOECHO)mkdir -p `dirname $@`
	$(NOECHO)$(CC)  $(CFLAGS) $(ONLY_CFLAGS)   -c $< -o ${LOCAL_OBJS_DIR}/$<.o

${LOCAL_OBJS_DIR}/%.cc.o : %.cc
	$(NOECHO)mkdir -p `dirname $@`
	$(NOECHO)$(CPP) $(CFLAGS) $(ONLY_CPPFLAGS) -c $< -o ${LOCAL_OBJS_DIR}/$<.o

${LOCAL_OBJS_DIR}/%.cpp.o : %.cpp
	$(NOECHO)mkdir -p `dirname $@`
	$(NOECHO)$(CPP) $(CFLAGS) $(ONLY_CPPFLAGS) -c $< -o ${LOCAL_OBJS_DIR}/$<.o

${LOCAL_OBJS_DIR}/%.cxx.o : %.cxx
	$(NOECHO)mkdir -p `dirname $@`
	$(NOECHO)$(CPP) $(CFLAGS) $(ONLY_CPPFLAGS) -c $< -o ${LOCAL_OBJS_DIR}/$<.o
