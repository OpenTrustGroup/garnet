# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a executable program.

LOCAL_OBJS_DIR ?= out
TRACE ?= false

SRC_C_OBJ=$(patsubst %,${LOCAL_OBJS_DIR}/%.o,${SRC_C})

ifeq (${enable_deprecated_syscalls}, true)
  CFLAGS += -DENABLE_TRUSTY_DEPRECATED_SYSCALL
endif

# Can invoke the HA with some arg (RUN_ARG).
ifneq (${RUN_ARG},)
  APP_ARG=--run_ha_param "${RUN_ARG}"
endif

ifneq (${DEVICE_IP},)
  DEVICE_IP_ARG=--ssh_ip ${DEVICE_IP}
endif

ifeq (${TRACE}, true)
  APP_ARG += --with_tracer
  EXT_LIB += ${SDK_HEE_HOME}/nbl_trace_provider/lib/libnbl_trace_provider.so
endif

ifneq (${SD_PRIVILEGE_FILE},)
  SD_PRIVILEGE_FILE_ARG=--sd_privilege_file "${SD_PRIVILEGE_FILE}"
endif

# library dependency for enabling TA lifecycle management
EXT_LIB += \
        -Wl,-whole-archive \
        ${SDK_HEE_HOME}/nbl_ta_controller/lib/libnbl_ta_controller.a \
        -Wl,-no-whole-archive \
        ${SDK_HEE_HOME}/nbl_loader/lib/libfidl.a \
        ${SDK_HEE_HOME}/nbl_fwk_hee/lib/libnbl_fwk_hee.a \
        ${SDK_HEE_HOME}/nebula-cpp/lib/libnebula-base.a \
        ${SDK_HEE_HOME}/nebula-cpp/lib/libnebula-ipc.a \
        ${SDK_HEE_HOME}/nebula-cpp/lib/libnebula-rpc.a \
        ${SDK_HEE_HOME}/pretty/lib/libpretty.a \
        ${SDK_HEE_HOME}/trusty/lib/liblibc-trusty.a \
        ${SDK_HEE_HOME}/trusty/lib/libstorage.a \
        ${SDK_HEE_HOME}/trusty/lib/librng.a \
        ${SDK_HEE_HOME}/trusty/lib/libhwkey.a \
        ${SDK_HEE_HOME}/trusty/lib/libmtee_api.a \
        ${SDK_HEE_HOME}/trusty/lib/libkeymaster.a \
        ${SDK_HEE_HOME}/zircon/lib/libfbl.a \
        ${SDK_HEE_HOME}/zircon/lib/libzx.a \
        ${SDK_HEE_HOME}/component/lib/libfidl.a \
        ${SDK_HEE_HOME}/app/lib/libcpp.a \
        ${SDK_HEE_HOME}/mem/lib/libfidl.a \
        ${SDK_HEE_HOME}/fidl/lib/libfidl.a \
        ${SDK_HEE_HOME}/fidl_cpp/lib/libcpp.a \
        ${SDK_HEE_HOME}/fs/lib/libfs.a \
        ${SDK_HEE_HOME}/svc/lib/libcpp.a \
        ${SDK_HEE_HOME}/async/lib/libasync.a \
        ${SDK_HEE_HOME}/async-cpp/lib/libasync.cpp.a \
        ${SDK_HEE_HOME}/async-loop/lib/libasync-loop.a \
        ${SDK_HEE_HOME}/async-loop-cpp/lib/libasync-loop.cpp.a \
        ${SDK_HEE_HOME}/nbl_nsc/lib/libnbl_nsc.a


# This target must be first one.
# - clang use ${CC} as ld.
all: ${SRC_C_OBJ}
	$(NOECHO)$(CPP) $(LDFLAGS) ${SRC_C_OBJ} ${EXT_LIB} -o ${LOCAL_OBJS_DIR}/${TARGET}.unstrip
	$(NOECHO)$(STRIP) ${LOCAL_OBJS_DIR}/${TARGET}.unstrip ${LOCAL_OBJS_DIR}/${TARGET}

clean:
	rm -rf ${LOCAL_OBJS_DIR}

run:
	${SDK_HOME}/run/run-ha-in-qemu.sh -e ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG}

install-device:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_create_sd.sh ${SD_PRIVILEGE_FILE_ARG} -f -d
	${SDK_HOME}/run/otrp/debug_tool/secmgr_install_ha.sh --ha_path ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG} -f -d

run-device:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_debug.sh --ha_path ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG} ${SD_PRIVILEGE_FILE_ARG} -f -d

install-qemu:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_create_sd.sh ${DEVICE_IP_ARG} ${SD_PRIVILEGE_FILE_ARG} -f
	${SDK_HOME}/run/otrp/debug_tool/secmgr_install_ha.sh --ha_path ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG} ${DEVICE_IP_ARG} -f

run-qemu:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_debug.sh --ha_path ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG} ${DEVICE_IP_ARG} ${SD_PRIVILEGE_FILE_ARG} -f

provision:
	${SDK_HOME}/run/otrp/provision_tool/provision_create_ha_by_config.sh -c ${SDK_HOME}/run/otrp/key_store_test -e ${LOCAL_OBJS_DIR}/${TARGET} -o ${LOCAL_OBJS_DIR}/provision ${SD_PRIVILEGE_FILE_ARG}

	#build other key
ifneq (${CONFIG_FOLDER_PATH},)
	${SDK_HOME}/run/otrp/provision_tool/provision_create_ha_by_config.sh -c ${CONFIG_FOLDER_PATH} -e ${LOCAL_OBJS_DIR}/${TARGET} -o ${LOCAL_OBJS_DIR}/provision ${SD_PRIVILEGE_FILE_ARG}
endif

provision-run-qemu:
ifneq (${CONFIG_FOLDER_PATH},)
	${SDK_HOME}/run/otrp/provision_tool/provision_create_ha_by_config.sh -q "" -c ${CONFIG_FOLDER_PATH} -e ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG} -o ${LOCAL_OBJS_DIR}/provision ${SD_PRIVILEGE_FILE_ARG}
else
	${SDK_HOME}/run/otrp/provision_tool/provision_create_ha_by_config.sh -q "" -c ${SDK_HOME}/run/otrp/key_store_test -e ${LOCAL_OBJS_DIR}/${TARGET} ${APP_ARG} -o ${LOCAL_OBJS_DIR}/provision ${SD_PRIVILEGE_FILE_ARG}
endif

provision-run-device:
ifneq (${CONFIG_FOLDER_PATH},)
	${SDK_HOME}/run/otrp/provision_tool/provision_create_ha_by_config.sh -d -c ${CONFIG_FOLDER_PATH} -e ${LOCAL_OBJS_DIR}/${TARGET} -o ${LOCAL_OBJS_DIR}/provision ${SD_PRIVILEGE_FILE_ARG}
else
	${SDK_HOME}/run/otrp/provision_tool/provision_create_ha_by_config.sh -d -c ${SDK_HOME}/run/otrp/key_store_test -e ${LOCAL_OBJS_DIR}/${TARGET} -o ${LOCAL_OBJS_DIR}/provision ${SD_PRIVILEGE_FILE_ARG}
endif

# After install a HA with SD, then changed SD privilege can not be UPDATED.
# So, add target "delete-sd" to delete sd with TA, and then install SD and TA again.

# delete sd of TA in Qemu
delete-qemu-sd:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_delete_sd.sh --temp_folder out/debug --sd_name "debug_sd"

delete-sd : delete-qemu-sd

# delete sd of TA in Device.
delete-device-sd:
	${SDK_HOME}/run/otrp/debug_tool/secmgr_delete_sd.sh --temp_folder out/debug --sd_name "debug_sd" -d
