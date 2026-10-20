# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/mt8676-board.c \
    $(LOCAL_DIR)/mt8676-wdt.c \
    $(LOCAL_DIR)/mt8676-ramfb.c \
    $(LOCAL_DIR)/mt8676-framebuffer.c \
    $(LOCAL_DIR)/mt8676-spi.c \
    $(LOCAL_DIR)/mt8676-gpio.c \
    $(LOCAL_DIR)/mt8676-monitor.c

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/dev/soc/grt/mt8676

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
