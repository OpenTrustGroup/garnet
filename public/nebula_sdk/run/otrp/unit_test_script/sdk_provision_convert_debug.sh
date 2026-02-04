#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the SD test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/convert_debug_test

SD_NAME=debug_sd
exe_path=$basepath/../../../examples/hee/libc/out/test-libc

key_store_path=$basepath/../key_store_test

rm -rf base_out_path/*
mkdir -p $base_out_path

$basepath/../provision_tool/provision_convert_debug.sh --in_file_path $basepath/../provision_tool/out/provision_install_ha_test-lib.json --spaik_private_key_pem_path $key_store_path/spaik/spaik_private_key_pkcs8.pem --out_file_folder $base_out_path

if [ -z "$base_out_path/provision_create_sd.json" ]; then
    echo "SDK test provision convert debug test failed. File provision_create_sd.json not exist."
    exit 1
fi
if [ -z "$base_out_path/provision_get_device_state.json" ]; then
    echo "SDK test provision convert debug test failed. File provision_get_device_state.json not exist."
    exit 1
fi
if [ -z "$base_out_path/provision_ha.json" ]; then
    echo "SDK test provision convert debug test failed. File provision_ha.json not exist."
    exit 1
fi

new_md5_result=$(md5sum $base_out_path/ha)
old_md5_result=$(md5sum $exe_path)

new_md5=${new_md5_result% *}
old_md5=${old_md5_result% *}

if [ -z $new_md5 ] || [ -z $old_md5 ]; then
    echo "SDK test provision convert debug test failed. Get MD5 failed."
    exit 1
fi

if [ "$new_md5" != "$old_md5" ]; then
    echo "SDK test provision convert debug test failed. Get MD5 different."
    exit 1
fi

echo -e "sdk_test_provision_convert_debug success."
