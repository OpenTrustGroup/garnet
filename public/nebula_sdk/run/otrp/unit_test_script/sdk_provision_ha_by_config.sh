#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the SD where "hello world" HA is located. Reinstall HA through the configuration file
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_provision_config_ha_out

SD_NAME=debug_sd
exe_path=$basepath/../../../examples/hee/libc/out/test-libc
key_store_path=$basepath/../key_store_test

rm -rf base_out_path/*

#provision_create_ha_by_config.sh
$basepath/../provision_tool/provision_create_ha_by_config.sh -c $key_store_path -e ${exe_path} -o ${base_out_path}

#push data to qemu
    $basepath/../provision_tool/push_qemu.sh $base_out_path/key_store_test /root/ree/secmgr/secmgr_privision_config_test
    $basepath/../provision_tool/push_qemu.sh $base_out_path/delete_sd/key_store_test /root/ree/secmgr/secmgr_privision_config_test
#install data
    $basepath/../provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_privision_config_test
