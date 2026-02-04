# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# Inject data to TEE's TA.

SDK_HOME ?= ../../../..

# include the common variables.
include ${SDK_HOME}/make/ree-common.mk

# define the options about compiling.
SRC_C = main.c

CFLAGS += \
	-I${SDK_REE_HOME}/include/trusty \
	-I${SDK_REE_HOME}/include/nbl_fwk_ree \
	-I./

LDFLAGS += -lpthread -Wall -Werror \
	-L${SDK_REE_HOME}/lib -lnbl_trusty -lnbl_fwk_ree -lnbl_license_ree

TARGET = simple_kmsetkey

include ${SDK_HOME}/make/ree-program.mk

# If the NDK is user's own, then set "ANDROID_NDK_HOME".
# If NOT, the "ndk-common.mk" will try to find it.
# ANDROID_NDK_HOME := ?

include ${SDK_HOME}/make/ndk-common.mk

REE_TRUSTY_PATH:="./"
MY_NDK_OPTIONS:=NDK_PROJECT_PATH=${REE_TRUSTY_PATH} \
    APP_BUILD_SCRIPT:=${REE_TRUSTY_PATH}/Android.mk \
    NDK_APPLICATION_MK:=${REE_TRUSTY_PATH}/Application.mk \
    NDK_OUT:=./out-ndk \
	SDK_ANDROID_HOME:=${SDK_ANDROID_HOME} \

all-ndk:
	@${ANDROID_NDK_HOME}/ndk-build ${MY_NDK_OPTIONS}

clean-ndk:
	@${ANDROID_NDK_HOME}/ndk-build clean ${MY_NDK_OPTIONS}
	@rm -rf libs out-ndk

# TODO : run is NOT tested.
# - because the nbl_fwk is NOT implemented in Android, so only test the "make", not run.
EXE_NAME=nmt_ree
run-ndk:
	which adb; \
	if [ $$? != 0 ]; then \
	echo "please install adb"; \
	else \
	sudo adb kill-server && sudo adb devices && \
	adb root && adb remount && \
	adb push out-ndk/local/arm64-v8a/${EXE_NAME} /data && \
	adb shell /data/${EXE_NAME} ; \
	fi

# Build this ca to Linux(Qemu) and Android at the same time.
all: all-ndk
