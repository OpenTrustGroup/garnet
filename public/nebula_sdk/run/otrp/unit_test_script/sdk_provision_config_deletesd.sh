#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the config SD test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_provision_config_ha_out

rm -rf $base_out_path/key_store_test/provision_create_sd.json
rm -rf $base_out_path/key_store_test/provision_install_test-libc.json


#push data to qemu
    $basepath/../provision_tool/push_qemu.sh $base_out_path/delete_sd/key_store_test /root/ree/secmgr/secmgr_privision_config_delete_sd_test
    $basepath/../provision_tool/push_qemu.sh $base_out_path/key_store_test /root/ree/secmgr/secmgr_privision_config_delete_sd_test
#install data
    $basepath/../provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_privision_config_delete_sd_test
