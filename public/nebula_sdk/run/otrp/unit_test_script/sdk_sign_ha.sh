#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the SD test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/sign_ha

SD_NAME=debug_sd
exe_path=$basepath/../../../examples/hee/libc/out/test-libc
signed_exe_path=$base_out_path/signed_ha
key_store_path=$basepath/../key_store_test

mkdir -p $base_out_path
rm -rf base_out_path/*

#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --far_tool_file $basepath/../elf_tool/far --ha_path ${exe_path} --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $signed_exe_path

#generate get_device_state data
$basepath/../sign_tool/secmgr_get_config.sh --enc_ha_path $signed_exe_path --out_file $base_out_path/config.json

grep "f3573a54-4af5-4d89-a6f2-11d36aa499d5" $base_out_path/config.json >/dev/null
if [ $? -eq 0 ]; then
    echo "Test sdk sign ha success!"
else
    echo "Test sdk sign ha failed!"
    exit 1
fi
