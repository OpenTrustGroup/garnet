# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
	$(LOCAL_DIR)/ufs.c \
	$(LOCAL_DIR)/ufs_core.c \
	$(LOCAL_DIR)/ufs_hcd.c \
	$(LOCAL_DIR)/ufs_interface.c \
	$(LOCAL_DIR)/ufs_quirks.c \
	$(LOCAL_DIR)/ufs_utils.c

MODULE_STATIC_LIBS := \
	system/ulib/ddk \
	system/ulib/sync \
	system/ulib/pretty \

MODULE_LIBS := \
	system/ulib/driver \
	system/ulib/c \
	system/ulib/zircon \

MODULE_HEADER_DEPS := \
    system/dev/soc/grt/mt8675 \

include make/module.mk
