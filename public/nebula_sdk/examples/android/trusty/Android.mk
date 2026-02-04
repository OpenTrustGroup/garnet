# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libnbl_trusty
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libnbl_trusty.a
LOCAL_EXPORT_C_INCLUDES := $(SDK_ANDROID_HOME)/include

include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := test-tipc
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := tipc_test.c
LOCAL_STATIC_LIBRARIES := libnbl_trusty

include $(BUILD_EXECUTABLE)
