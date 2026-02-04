# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# Sample for Install TA in REE.
# libc is a TA which will be built and made to a group provision to a folder.
# And then this application will install provision files to TEE.

SDK_HOME ?= ../../..

# include the common variables.
include ${SDK_HOME}/make/ree-common.mk

# define the options about compiling.
SRC_C = main.c

CFLAGS += \
	-I${SDK_REE_HOME}/include/trusty \
	-I${SDK_REE_HOME}/include/nbl_fwk_ree \

LDFLAGS += -lpthread -Wall -Werror \
	-L${SDK_REE_HOME}/lib -lnbl_trusty -lnbl_fwk_ree -lnbl_loader_ree -lnbl_secmgr_ree

TARGET = test-nbl_secmgr_provision

include ${SDK_HOME}/make/ree-program.mk

run-qemu:
	make -C libc
	${SDK_HOME}/run/otrp/provision_tool/test/secmgr_provision_test2.sh -o data
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_get_device_state.json -o /root/ree/secmgr
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_create_sd.json -o /root/ree/secmgr
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_install_ha_test-lib.json -o /root/ree/secmgr
	${SDK_HOME}/run/run-ca-in-qemu.sh -e out/${TARGET} -a "/root/ree/secmgr"

run-qemu-delete-sd:
	${SDK_HOME}/run/otrp/provision_tool/test/secmgr_provision_test2_delete_sd.sh -o data
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_get_device_state.json -o /root/ree/secmgr-delete-sd
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_delete_sd.json -o /root/ree/secmgr-delete-sd
	${SDK_HOME}/run/run-ca-in-qemu.sh -e out/${TARGET} -a "/root/ree/secmgr-delete-sd"

clean:data-clean

data-clean:
	make clean -C libc
	rm -rf data