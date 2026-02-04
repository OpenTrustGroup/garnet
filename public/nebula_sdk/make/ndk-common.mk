# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make for CA running in Android.
# 1, Use ndk to build.

SHELL=/bin/bash

# check SDK_HOME
ifeq ($(origin SDK_HOME), undefined)
  $(error "Set the SDK_HOME to sdk root directory.")
endif

# check ANDROID_NDK_HOME
exist = false

# If the ndk directory is NOT defined, then the script will try to find the path of it.
ifeq ($(origin ANDROID_NDK_HOME), undefined)
  ifeq ($(origin FUCHSIA_DIR), undefined) # If used by third-paarty developers, and ndk is in SDK.
    ANDROID_NDK_HOME = $(SDK_HOME)/toolchains/android-ndk
  endif
endif

exist = $(shell if [ -d ${ANDROID_NDK_HOME} ]; then echo "true"; else echo "false"; fi;)
ifeq (${exist}, true)
  $(info Find Android NDK in ${ANDROID_NDK_HOME})
else
  $(error "Set the ANDROID_NDK_HOME to Android NDK root directory.")
endif

#define path of headers and libraries.
SDK_ANDROID_HOME=${SDK_HOME}/android
