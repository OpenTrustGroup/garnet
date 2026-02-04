# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# Sample for Install in REE.
# run-qemu:
# secmgr_provision_test.sh create *.json files, and install them into REE (Linux in Qemu) by script.
# - 1. provision_get_device_state.json : get the device state
# - 2. provision_create_sd.json : create a secure domain.
# - 3. provision_install_ha_test-lib.json : installed HA(examples/hee/libc).
# run-ca-in-qemu.sh will install this CA into REE and install provision in folder to TEE.

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

TARGET = test-nbl_secmgr

include ${SDK_HOME}/make/ree-program.mk

clean:
	rm -rf data out

run-qemu:
	make -C ../../hee/libc
	${SDK_HOME}/run/otrp/provision_tool/test/secmgr_provision_test.sh -o data
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_get_device_state.json -o /root/ree/secmgr
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_create_sd.json -o /root/ree/secmgr
	${SDK_HOME}/run/install-file-in-qemu.sh -e data/provision_install_ha_test-lib.json -o /root/ree/secmgr
	${SDK_HOME}/run/run-ca-in-qemu.sh -e out/${TARGET} -a "/root/ree/secmgr"
