#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used test inject_secmgr_key
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_provision_config_ha_out

exe_path=$basepath/kmsetkey
key_folder_name=key_store_test
key_store_path=$basepath/../../../run/otrp/$key_folder_name

rm -rf base_out_path/*

#provision_create_ha_by_config.sh
$basepath/../../../run/otrp/provision_tool/provision_create_ha_by_config.sh -c $key_store_path -e ${exe_path} -o ${base_out_path}

#qemu
#push data to qemu
    $basepath/../../../run/otrp/provision_tool/push_qemu.sh $base_out_path/$key_folder_name /root/ree/secmgr/kmsetkey
#install data
    $basepath/../../../run/otrp/provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/kmsetkey
# run ta qemu
ssh -o StrictHostKeyChecking=no root@192.168.3.53 nbl_daemon -l 7445ca05-35d5-4698-bb9a-0d840f9a5fcf
