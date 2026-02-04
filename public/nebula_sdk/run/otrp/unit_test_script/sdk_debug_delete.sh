#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# the delete test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_delete_clean

mkdir -p $base_out_path
rm -rf $base_out_path/*

$basepath/../debug_tool/secmgr_init.sh $param --temp_folder $base_out_path
$basepath/../debug_tool/secmgr_delete_ha.sh --sd_name test_sd_name --ha_id f3573a54-4af5-4d89-a6f2-11d36aa499d5 $local_param -l --temp_folder $base_out_path
$basepath/../debug_tool/secmgr_delete_sd.sh --sd_name test_sd_name2 $param -l --temp_folder $base_out_path

$basepath/../debug_tool/secmgr_ls.sh -v --temp_folder $base_out_path
