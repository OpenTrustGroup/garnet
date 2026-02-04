#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the SD test
# $1 ssh ip
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/provision_delete_sd_t

SD_NAME=debug_sd
exe_path=$basepath/../../../examples/hee/libc/out/test-libc

key_store_path=$basepath/../key_store_test

mkdir -p $base_out_path
rm -rf base_out_path/*

#generate get_device_state data
$basepath/../provision_tool/provision_get_device_state.sh --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --tsm_cert_path $key_store_path/tsm/sign_cert/tsm_sign.pem --tsm_ocsp_path $key_store_path/tsm/sign_cert/ocsp/tsm_sign_resp.pem --out_file_path $base_out_path/provision_get_device_state.json

$basepath/../provision_tool/provision_delete_sd.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name ${SD_NAME} --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_delete_sd.json --delete_ha "true"

#push data to qemu
    if [ -n "$1" ]; then
        $basepath/../provision_tool/push_qemu.sh $base_out_path /root/ree/secmgr/secmgr_privision_delete_sd_test $1
    else
        $basepath/../provision_tool/push_qemu.sh $base_out_path /root/ree/secmgr/secmgr_privision_delete_sd_test
    fi
#install data
    if [ -n "$1" ]; then
        $basepath/../provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_privision_delete_sd_test --ssh_ip $1
    else
        $basepath/../provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_privision_delete_sd_test
    fi
