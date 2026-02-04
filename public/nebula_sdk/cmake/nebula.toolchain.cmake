# Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.6.0)

# Inhibit all of CMake's own NDK handling code.
set(CMAKE_SYSTEM_VERSION 1)

set(NEBULA_SDK_HOME "$ENV{SDK_HOME}")

if("$ENV{FUCHSIA_DIR}" STREQUAL "")
  set(TOOLCHAIN_HOME "${NEBULA_SDK_HOME}/toolchains/clang")
else()
  message(STATUS "FUCHSIA_DIR=$ENV{FUCHSIA_DIR}")
  set(TOOLCHAIN_HOME "$ENV{FUCHSIA_DIR}/buildtools/linux-x64/clang")
endif()

message(STATUS "NEBULA_SDK_HOME=${NEBULA_SDK_HOME}")
message(STATUS "TOOLCHAIN_HOME=${TOOLCHAIN_HOME}")

if("${NEBULA_SDK_HOME}" EQUAL "")
  message(FATAL_ERROR "Set the SDK_HOME to sdk root directory.")
endif()

if(NOT EXISTS "${TOOLCHAIN_HOME}")
  message(FATAL_ERROR "Nebula toolchain not found: ${TOOLCHAIN_HOME}")
endif()

set(SDK_HEE_HOME "${NEBULA_SDK_HOME}/hee")


set(NEBULA_TOOLCHAIN_PREFIX "${TOOLCHAIN_HOME}/bin/llvm-")
set(NEBULA_CC "${TOOLCHAIN_HOME}/bin/clang")
set(NEBULA_CPP "${TOOLCHAIN_HOME}/bin/clang++")
set(NEBULA_LD "${TOOLCHAIN_HOME}/bin/ld.lld")
set(NEBULA_AR "${NEBULA_TOOLCHAIN_PREFIX}ar")
set(NEBULA_RANLIB "${NEBULA_TOOLCHAIN_PREFIX}ranlib")

# clang use objcopy to strip ELF. llvm-objcopy <from> <to>.
#set(NEBULA_STRIP "${TOOLCHAIN_HOME}/bin/llvm-objcopy --strip-all-gnu")


set(NEBULA_COMPILER_FLAGS)
set(NEBULA_COMPILER_FLAGS_CXX)
set(NEBULA_COMPILER_FLAGS_DEBUG)
set(NEBULA_COMPILER_FLAGS_RELEASE)
set(NEBULA_LINKER_FLAGS)

# Compile flag only for C.
# --sysroot=<dir> : It will search headers and libs in <dir>/usr/include and <dir>/usr/lib.
# --target=aarch64-fuchsia : Clang will read the header and lib from this target folder.
# -no-canonical-prefixes : don't expand any symbolic links.
list(APPEND NEBULA_COMPILER_FLAGS
  -D__NEBULA__
  -no-canonical-prefixes)

# Compile Flag only for C++.
list(APPEND NEBULA_COMPILER_FLAGS_CXX
  -D__NEBULA__
  -no-canonical-prefixes)


#list(APPEND NEBULA_COMPILER_FLAGS_CXX
#  -fno-rtti
#  -fno-exceptions)

# Link flag.
# --icf=all : enable identical code folding.
# -lfdio -lzircon : all programs needs the libraries.
# -z max-page-size : is for linker, but gcc will transfer the "-z" to linker without the "-Wl,". It will reduce the size of elf.
list(APPEND NEBULA_LINKER_FLAGS
  -D__NEBULA__
  -Wl,--icf=all
  -z max-page-size=4096
  -L${SDK_HEE_HOME}/lib
  -lfdio
  -lzircon
  -L${SDK_HEE_HOME}/nbl_fwk_hee/lib
  -lnbl_fwk_hee)

# compile flags for debug/release
list(APPEND NEBULA_COMPILER_FLAGS_DEBUG -O0)
list(APPEND NEBULA_COMPILER_FLAGS_DEBUG -g)
list(APPEND NEBULA_COMPILER_FLAGS_RELEASE -O2)

# Convert these lists into strings.
string(REPLACE ";" " " NEBULA_COMPILER_FLAGS         "${NEBULA_COMPILER_FLAGS}")
string(REPLACE ";" " " NEBULA_COMPILER_FLAGS_CXX     "${NEBULA_COMPILER_FLAGS_CXX}")
string(REPLACE ";" " " NEBULA_COMPILER_FLAGS_DEBUG   "${NEBULA_COMPILER_FLAGS_DEBUG}")
string(REPLACE ";" " " NEBULA_COMPILER_FLAGS_RELEASE "${NEBULA_COMPILER_FLAGS_RELEASE}")
string(REPLACE ";" " " NEBULA_LINKER_FLAGS           "${NEBULA_LINKER_FLAGS}")

# Touch toolchain variable to suppress "unused variable" warning.
# This happens if CMake is invoked with the same command line the second time.
if(CMAKE_TOOLCHAIN_FILE)
endif()

# Standard cross-compiling stuff.
set(CMAKE_SYSTEM_NAME Linux)

# ONLY doesn't do anything when CMAKE_FIND_ROOT_PATH is empty. Without this,
# CMake will wrongly search host sysroots for headers/libraries. The actual path
# used here is fairly meaningless since CMake doesn't handle the NDK sysroot
# layout (per-arch and per-verion subdirectories for libraries), so find_library
# is handled separately by CMAKE_SYSTEM_LIBRARY_PATH.
list(APPEND CMAKE_FIND_ROOT_PATH "${SDK_HEE_HOME}")

# search for programs in the build host directories (not necessary)
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ABI.
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(NEBULA_COMPILER_TARGET aarch64-fuchsia)

# NB: This variable causes CMake to automatically pass --sysroot to the
# toolchain. Studio currently relies on this to recognize Android builds. If
# this variable is removed, ensure that flag is still passed.
# TODO: Teach Studio to recognize Android builds based on --target.
set(CMAKE_SYSROOT "${SDK_HEE_HOME}")

# Clang can fail to compile if CMake doesn't correctly supply the target and
# external toolchain, but to do so, CMake needs to already know that the
# compiler is clang. Tell CMake that the compiler is really clang, but don't
# use CMakeForceCompiler, since we still want compile checks. We only want
# to skip the compiler ID detection step.
set(CMAKE_C_COMPILER_ID_RUN TRUE)
set(CMAKE_CXX_COMPILER_ID_RUN TRUE)
set(CMAKE_C_COMPILER_ID Clang)
set(CMAKE_CXX_COMPILER_ID Clang)
set(CMAKE_C_COMPILER_VERSION 7.0)
set(CMAKE_CXX_COMPILER_VERSION 7.0)
set(CMAKE_C_STANDARD_COMPUTED_DEFAULT 11)
set(CMAKE_CXX_STANDARD_COMPUTED_DEFAULT 14)


set(CMAKE_C_COMPILER "${TOOLCHAIN_HOME}/bin/clang")
set(CMAKE_C_COMPILER_TARGET ${NEBULA_COMPILER_TARGET} CACHE STRING "")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_HOME}/bin/clang++")
set(CMAKE_CXX_COMPILER_TARGET ${NEBULA_COMPILER_TARGET} CACHE STRING "")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_HOME}/bin/clang")
set(CMAKE_ASM_COMPILER_TARGET ${NEBULA_COMPILER_TARGET} CACHE STRING "")
set(CMAKE_AR "${TOOLCHAIN_HOME}/bin/llvm-ar" CACHE PATH "")
set(CMAKE_LINKER "${TOOLCHAIN_HOME}/bin/ld.lld" CACHE PATH "")
set(CMAKE_NM "${TOOLCHAIN_HOME}/bin/llvm-nm" CACHE PATH "")
set(CMAKE_OBJCOPY "${TOOLCHAIN_HOME}/bin/llvm-objcopy" CACHE PATH "")
set(CMAKE_OBJDUMP "${TOOLCHAIN_HOME}/bin/llvm-objdump" CACHE PATH "")
set(CMAKE_RANLIB "${TOOLCHAIN_HOME}/bin/llvm-ranlib" CACHE PATH "")

# Set or retrieve the cached flags.
# This is necessary in case the user sets/changes flags in subsequent
# configures. If we included the Android flags in here, they would get
# overwritten.
set(CMAKE_C_FLAGS ""
  CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_CXX_FLAGS ""
  CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_ASM_FLAGS ""
  CACHE STRING "Flags used by the compiler during all build types.")
set(CMAKE_C_FLAGS_DEBUG ""
  CACHE STRING "Flags used by the compiler during debug builds.")
set(CMAKE_CXX_FLAGS_DEBUG ""
  CACHE STRING "Flags used by the compiler during debug builds.")
set(CMAKE_ASM_FLAGS_DEBUG ""
  CACHE STRING "Flags used by the compiler during debug builds.")
set(CMAKE_C_FLAGS_RELEASE ""
  CACHE STRING "Flags used by the compiler during release builds.")
set(CMAKE_CXX_FLAGS_RELEASE ""
  CACHE STRING "Flags used by the compiler during release builds.")
set(CMAKE_ASM_FLAGS_RELEASE ""
  CACHE STRING "Flags used by the compiler during release builds.")
set(CMAKE_MODULE_LINKER_FLAGS ""
  CACHE STRING "Flags used by the linker during the creation of modules.")
set(CMAKE_SHARED_LINKER_FLAGS ""
  CACHE STRING "Flags used by the linker during the creation of dll's.")
set(CMAKE_EXE_LINKER_FLAGS ""
  CACHE STRING "Flags used by the linker.")

set(CMAKE_C_FLAGS             "${NEBULA_COMPILER_FLAGS} ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS           "${NEBULA_COMPILER_FLAGS_CXX} ${CMAKE_CXX_FLAGS}")
set(CMAKE_ASM_FLAGS           "${NEBULA_COMPILER_FLAGS} --target=aarch64-fuchsia ${CMAKE_ASM_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG       "${NEBULA_COMPILER_FLAGS_DEBUG} ${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG     "${NEBULA_COMPILER_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_ASM_FLAGS_DEBUG     "${NEBULA_COMPILER_FLAGS_DEBUG} ${CMAKE_ASM_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE     "${NEBULA_COMPILER_FLAGS_RELEASE} ${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE   "${NEBULA_COMPILER_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_ASM_FLAGS_RELEASE   "${NEBULA_COMPILER_FLAGS_RELEASE} ${CMAKE_ASM_FLAGS_RELEASE}")
set(CMAKE_SHARED_LINKER_FLAGS "${NEBULA_LINKER_FLAGS} ${CMAKE_SHARED_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${NEBULA_LINKER_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS    "${NEBULA_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")

