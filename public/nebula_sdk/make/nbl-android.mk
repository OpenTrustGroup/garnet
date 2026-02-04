# Copyright 2019 GoldenRiver Technology Co., Ltd. All rights reserved.

#####################################################################
# Include all libraries of Nebula system.
# Nebula's developers shall add nebula library into this file.
#
# Developers can include this file, and add the library into program.
# 1. Set "SDK_ANDROID_HOME" for this script.
#    (Example is Makefile in "examples/nbl_fwk/simple_fwk_ca/Makefile")
# 2. Dependency graph:
#   * libnbl_fwk_ree -----> libion/libion_mtk
#                      |--> libnbl_trusty
#                      +--> libgz_uree
#   * libnbl_loader_ree --> libnbl_fwk_ree
#   Developers can depend top library (such as libnbl_fwk_ree),
#   the depent libraries (such as libion) will be loaded automatically.
#
#   But the dependency relation JUST cat be transferred through ONE layer,
#   So developers shall add all children libraries/headers into its libs and headers.
#####################################################################

########################################
include $(CLEAR_VARS)

LOCAL_MODULE := libnbl_trusty
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libnbl_trusty.a
LOCAL_EXPORT_C_INCLUDES := $(SDK_ANDROID_HOME)/include  $(SDK_ANDROID_HOME)/include/trusty
LOCAL_EXPORT_C_INCLUDE_DIRS=$(LOCAL_EXPORT_C_INCLUDES)

include $(PREBUILT_STATIC_LIBRARY)

########################################
# On compiling, needed by libnbl_fwk_ree
# On running, ONLY needed by MTK platform.
include $(CLEAR_VARS)

LOCAL_MODULE := libion
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libion.a

include $(PREBUILT_STATIC_LIBRARY)

########################################
# On compiling, needed by libnbl_fwk_ree
# On running, ONLY needed by MTK platform.
include $(CLEAR_VARS)

LOCAL_MODULE := libion_mtk
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libion_mtk.a

include $(PREBUILT_STATIC_LIBRARY)

########################################
include $(CLEAR_VARS)

LOCAL_MODULE := libgz_uree
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libgz_uree.a

include $(PREBUILT_STATIC_LIBRARY)

########################################
include $(CLEAR_VARS)

LOCAL_MODULE := libnbl_fwk_ree
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libnbl_fwk_ree.a
LOCAL_EXPORT_C_INCLUDES := $(SDK_ANDROID_HOME)/include $(SDK_ANDROID_HOME)/include/trusty
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_EXPORT_C_INCLUDES)
LOCAL_EXPORT_STATIC_LIBRARIES := libnbl_trusty libion libion_mtk libgz_uree

include $(PREBUILT_STATIC_LIBRARY)

########################################
include $(CLEAR_VARS)

LOCAL_MODULE := libnbl_loader_ree
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libnbl_loader_ree.a
LOCAL_EXPORT_C_INCLUDES := $(SDK_ANDROID_HOME)/include $(SDK_ANDROID_HOME)/include/trusty
LOCAL_EXPORT_C_INCLUDE_DIRS=$(LOCAL_EXPORT_C_INCLUDES)
LOCAL_EXPORT_STATIC_LIBRARIES := libnbl_fwk_ree libnbl_trusty libion libion_mtk libgz_uree

include $(PREBUILT_STATIC_LIBRARY)

########################################

LOCAL_MODULE := libnbl_license_ree
LOCAL_SRC_FILES := $(SDK_ANDROID_HOME)/libs/$(TARGET_ARCH_ABI)/libnbl_license_ree.a
LOCAL_EXPORT_C_INCLUDES := $(SDK_ANDROID_HOME)/include $(SDK_ANDROID_HOME)/include/trusty
LOCAL_EXPORT_C_INCLUDE_DIRS=$(LOCAL_EXPORT_C_INCLUDES)
LOCAL_EXPORT_STATIC_LIBRARIES := libnbl_fwk_ree libnbl_trusty libion libion_mtk libgz_uree

include $(PREBUILT_STATIC_LIBRARY)

########################################