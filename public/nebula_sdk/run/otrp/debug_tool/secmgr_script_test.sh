#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)

function usage()
{
    printf 'usage:      Test script.Used to test other secmgr scripts.\n'
    printf '  -d	Run in the device when the "-d" parameter is set, otherwise run in QEMU.\n'
    printf '  -h	Show help.\n'
    printf '  -v	Full log output.\n'
}

while getopts "hdv" opt; do
  case "${opt}" in
    d) DEVICE=1;;
    v) VERBOSE=1;;
    h) usage 1>&2 ; exit 0 ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done

param=""
if [ ! -z "$DEVICE" ]; then
param=$param" -d"
fi

if [ ! -z "$VERBOSE" ]; then
param+=" -v"
fi
${basepath}/secmgr_init.sh $param --temp_folder $basepath/debug
${basepath}/secmgr_create_sd.sh --sd_name test_sd_name $param -l --temp_folder $basepath/debug
${basepath}/secmgr_create_sd.sh --sd_name test_sd_name2 $param -l --temp_folder $basepath/debug
${basepath}/secmgr_install_ha.sh --sd_name test_sd_name --ha_id 905bdb85-2dc5-429d-c952-723542111101 --ha_path $basepath/../../../examples/hee/libc/out/test-libc $param -l --temp_folder $basepath/debug

${basepath}/secmgr_run_ha.sh --ha_id 905bdb85-2dc5-429d-c952-723542111101 $param
${basepath}/secmgr_ls.sh $param -l --temp_folder $basepath/debug
${basepath}/secmgr_delete_ha.sh --sd_name test_sd_name --ha_id 905bdb85-2dc5-429d-c952-723542111101 $param -l --temp_folder $basepath/debug
${basepath}/secmgr_delete_sd.sh --sd_name test_sd_name2 $param -l --temp_folder $basepath/debug
${basepath}/secmgr_clean.sh $param -l --temp_folder $basepath/debug
echo "test finish"
