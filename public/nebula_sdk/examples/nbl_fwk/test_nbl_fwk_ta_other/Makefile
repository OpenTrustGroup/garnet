# Copyright 2018 GoldenRiver Technology Co., Ltd.

SDK_HOME ?= ../../..

include ${SDK_HOME}/make/hee-common.mk

CFLAGS += -I${SDK_HEE_HOME}/trusty/include \
	-I${SDK_HEE_HOME}/nbl_fwk_hee/include

LDFLAGS += -L${SDK_HEE_HOME}/trusty/lib/ -llibc-trusty \
	-L${SDK_HEE_HOME}/nbl_fwk_hee/lib -lnbl_fwk_hee -lmtee_api

SRC_C = other_service.c manifest.c

TARGET = test_nbl_fwk_other

include ${SDK_HOME}/make/hee-program.mk

# It must be after "include ... hee-program.mk", because the first target must be all in it.

install-qemu:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_create_sd.sh --sd_name debug_sdname -f
	${SDK_HOME}/run/otrp/debug_tool/secmgr_install_ha.sh --ha_path out/${TARGET} \
	--sd_name debug_sdname --ha_id 093917a8-8583-4d8f-942f-f76eb21e3457 -f

install-device:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_create_sd.sh --sd_name debug_sdname -f -d
	${SDK_HOME}/run/otrp/debug_tool/secmgr_install_ha.sh --ha_path out/${TARGET} \
	--sd_name debug_sdname --ha_id 093917a8-8583-4d8f-942f-f76eb21e3457 -f -d
