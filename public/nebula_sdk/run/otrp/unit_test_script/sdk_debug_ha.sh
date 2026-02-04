#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the config SD test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_debug_ha
exe_path=$basepath/../../../examples/hee/libc/out/test-libc

mkdir -p $base_out_path/temp
rm -rf $base_out_path/*


${basepath}/../debug_tool/secmgr_init.sh $local_param --temp_folder $base_out_path/temp
${basepath}/../debug_tool/secmgr_create_sd.sh --sd_name test_sd_name $local_param -l --temp_folder $base_out_path/temp
${basepath}/../debug_tool/secmgr_create_sd.sh --sd_name test_sd_name2 $local_param -l --temp_folder $base_out_path/temp
${basepath}/../debug_tool/secmgr_install_ha.sh --sd_name test_sd_name --ha_path $exe_path $local_param -l --temp_folder $base_out_path/temp

${basepath}/../debug_tool/secmgr_run_ha.sh --ha_id f3573a54-4af5-4d89-a6f2-11d36aa499d5 $local_param
