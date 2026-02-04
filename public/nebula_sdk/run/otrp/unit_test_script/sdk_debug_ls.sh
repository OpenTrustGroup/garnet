#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# the debug ls  test
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/out/test_ls

mkdir -p $base_out_path
rm -rf $base_out_path/*

$basepath/../debug_tool/secmgr_ls.sh -v --temp_folder $base_out_path
