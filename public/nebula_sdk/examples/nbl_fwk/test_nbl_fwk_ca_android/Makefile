# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# Make the nbl_fwk.
# The real build file is Android.mk and Application.mk.

SDK_HOME ?= ../../..

# If the NDK is user's own, then set "ANDROID_NDK_HOME".
# If NOT, the "ndk-common.mk" will try to find it.
# ANDROID_NDK_HOME := ?

include ${SDK_HOME}/make/ndk-common.mk

REE_TRUSTY_PATH="./"
MY_NDK_OPTIONS = NDK_PROJECT_PATH=${REE_TRUSTY_PATH} \
    APP_BUILD_SCRIPT=${REE_TRUSTY_PATH}/Android.mk \
    NDK_APPLICATION_MK=${REE_TRUSTY_PATH}/Application.mk \
    NDK_OUT=./out \
	SDK_ANDROID_HOME=${SDK_ANDROID_HOME} \

all:
	@${ANDROID_NDK_HOME}/ndk-build ${MY_NDK_OPTIONS}

clean:
	@${ANDROID_NDK_HOME}/ndk-build clean ${MY_NDK_OPTIONS}
	@rm -rf libs out

# TODO : run is NOT tested.
# - because the nbl_fwk is NOT implemented in Android, so only test the "make", not run.
EXE_NAME=test-nbl_fwk
run:
	which adb; \
	if [ $$? != 0 ]; then \
	echo "please install adb"; \
	else \
	sudo adb kill-server && sudo adb devices && \
	adb root && adb remount && \
	adb push out/local/arm64-v8a/{EXE_NAME} /data && \
	adb shell LD_LIBRARY_PATH=/data:$$LD_LIBRARY_PATH /data/{EXE_NAME} ; \
	fi
