#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

nbl_daemon_path=${FUCHSIA_OUT_DIR}/build-ree-trusty-ndk/obj/local/arm64-v8a/nbl_daemon
device_secmgr_path=/data/nebula/secmgr
linux_secmgr_path=/root/ree/secmgr_unit_test/demo
sdk_out_path=${FUCHSIA_OUT_DIR}/arm64/sdks/nebula

function usage() {
  printf '  -d         : Running on a real device.\n'
  printf '  -i ssh_ip  : (Optional)qemu ssh ip. Default:192.168.3.53\n'
  printf '  -l location: (Optional)qemu provision location.\n'
  printf '  -h         : show help.\n'
}

while getopts "hdi:l:" opt; do
  case "${opt}" in
    d)
      RUN_DEVICE=1
      ;;
    i)
      SSH_IP=${OPTARG}
      ;;
    l)
      linux_secmgr_path=${OPTARG}
      ;;
    h) usage 1>&2 ; exit 0 ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done
#/root/ree/secmgr_unit_test/demo/
#push data to device/qemu
if [ -n "$RUN_DEVICE" ]; then
    adb root
    adb remount
    adb shell mkdir -p $device_secmgr_path/test_privision
    adb shell rm -rf  $device_secmgr_path/test_privision/*

    nbl_daemon_exist=`adb shell ps -A | grep nbl_daemon`
    if [ -z "$nbl_daemon_exist" ]; then
        echo "Push nbl_daemon to the device."
        adb push $nbl_daemon_path $device_secmgr_path
        adb shell chmod 777 $device_secmgr_path/nbl_daemon
        $sdk_out_path/run/otrp/provision_tool/push_android.sh ${FUCHSIA_OUT_DIR}/secmgr/secmgr_test_ha $device_secmgr_path/test_privision
        adb shell $device_secmgr_path/nbl_daemon -p $device_secmgr_path/test_privision
    else
        echo "nbl_daemon already exists."
        $sdk_out_path/run/otrp/provision_tool/push_android.sh ${FUCHSIA_OUT_DIR}/secmgr/secmgr_test_ha $device_secmgr_path/test_privision
        adb shell nbl_daemon -p $device_secmgr_path/test_privision
    fi
    echo "nbl_daemon install data finish."
else
    $sdk_out_path/run/otrp/provision_tool/push_qemu.sh ${FUCHSIA_OUT_DIR}/secmgr/secmgr_test_ha $linux_secmgr_path $SSH_IP
fi
