#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used test inject_secmgr_key
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_provision_config_ha_out

exe_path=$basepath/hello_ta/out/hello_ta
key_store_path=$basepath/new_test_key

rm -rf base_out_path/*

make -C $basepath/hello_ta

#provision_create_ha_by_config.sh
$basepath/../../../run/otrp/provision_tool/provision_create_ha_by_config.sh -c $key_store_path -e ${exe_path} -o ${base_out_path}

#qemu
#push data to qemu
    $basepath/../../../run/otrp/provision_tool/push_qemu.sh $base_out_path/new_test_key /root/ree/secmgr/inject_secmgr_key_test
#install data
    $basepath/../../../run/otrp/provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/inject_secmgr_key_test
# run ta qemu
ssh -o StrictHostKeyChecking=no root@192.168.3.53 nbl_daemon -l fec8eea5-4584-4d5c-bdc2-3f6ae8a0888f
