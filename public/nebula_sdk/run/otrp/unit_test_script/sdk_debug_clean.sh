#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# the clean test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_clean

mkdir -p $base_out_path
rm -rf $base_out_path/*

if [ -n "$1" ]; then
    $basepath/../debug_tool/secmgr_init.sh $param --temp_folder $base_out_path --ssh_ip $1
    $basepath/../debug_tool/secmgr_clean.sh $param -l --temp_folder $base_out_path --ssh_ip $1

    $basepath/../debug_tool/secmgr_ls.sh -v --temp_folder $base_out_path --ssh_ip $1
else
    $basepath/../debug_tool/secmgr_init.sh $param --temp_folder $base_out_path
    $basepath/../debug_tool/secmgr_clean.sh $param -l --temp_folder $base_out_path

    $basepath/../debug_tool/secmgr_ls.sh -v --temp_folder $base_out_path
fi
