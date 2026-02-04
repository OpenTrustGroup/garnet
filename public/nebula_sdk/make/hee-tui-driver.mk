# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a shared library for HEE.
# - TODO: Currently the dynamic library can not be loaded into HEE by loader.

CFLAGS += \
	-I${SDK_HEE_HOME}/nbl_drv/include \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include \

LDFLAGS += \
	-Wl,--no-undefined \
	-Wl,--whole-archive \
	${SDK_HEE_HOME}/nbl_drv/lib/libnbl_drv.a \
	-Wl,--no-whole-archive \

EXT_LIB += \
	${SDK_HEE_HOME}/lib/libdriver.so \
	${SDK_HEE_HOME}/lib/libddk.a \

include ${SDK_HOME}/make/hee-shared-lib.mk
