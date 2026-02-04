#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function help()
{
    echo "provision_create_ha [-ehdo]"
    echo "-e set path of executable file."
    echo "-o <output directory> if not set, directory is $basepath/out."
    echo "-h show help."
}

basepath=$(cd `dirname $0`; pwd)

# parse the arguments of scripts
ARGS=`getopt -o ho:e: -n "$0" -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -o)
            base_out_path=$2
            shift 2
            ;;
        -e)
            exe_path=$2
            shift 2
            ;;
        --)
            shift
            break
            ;;
        -h)
            help
            exit 0
            ;;
        *)
            help
            exit 1
            ;;
    esac
done

if [ -z "$base_out_path" ]; then
    base_out_path=$basepath/out
fi

if [ -z "$exe_path" ]; then
    echo "Must input path of executable file."
    exit 1
fi

if [ ! -f "$exe_path" ]; then
    echo "Path of executable file is invalid."
    exit 1
fi

# should remove it.
signed_exe_path=$base_out_path/signed_ha.json
created_exe_path=$base_out_path/provision_ha.json

SD_NAME=debug_sdname
key_store_path=$basepath/../key_store_test

mkdir -p $base_out_path

#generate get_device_state data
$basepath/provision_get_device_state.sh --tsm_private_key_path \
        $key_store_path/tsm/sign_cert/private_key.pem \
        --tsm_cert_path $key_store_path/tsm/sign_cert/tsm_sign.pem \
        --tsm_ocsp_path $key_store_path/tsm/sign_cert/ocsp/tsm_sign_resp.pem \
        --out_file_path $base_out_path/provision_get_device_state.json

#generate create sd data
$basepath/provision_create_sd.sh --tsm_id test.tsm.goldenrivertek.com \
        --sp_id debug_spid --hee_name test_tee_name --sd_name ${SD_NAME} \
        --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem \
        --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem \
        --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem \
        --out_file_path $base_out_path/provision_create_sd.json

#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --far_tool_file $basepath/../elf_tool/far \
        --ha_path ${exe_path} --sp_private_key_pem_path \
        $key_store_path/sp/sign_cert/private_key.pem \
        --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem \
        --out_file $signed_exe_path

# Create json
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com \
        --sp_id debug_spid \
        --hee_name test_tee_name --sd_name ${SD_NAME} \
        --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem \
        --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem \
        --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem \
        --ha_path ${signed_exe_path} \
        --out_file_path $created_exe_path

rm -f $signed_exe_path
