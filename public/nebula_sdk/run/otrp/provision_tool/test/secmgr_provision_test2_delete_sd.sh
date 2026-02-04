#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

function help()
{
    echo "demo.sh [-hdo]"
    echo "-d Run in the device when the '-d' parameter is set, otherwise run in QEMU."
    echo "-o <output directory> if not set, directory is $basepath/out."
    echo "-h show help."
}

basepath=$(cd `dirname $0`; pwd)/..
base_out_path=$basepath/out

# parse the arguments of scripts
ARGS=`getopt -o hdo: -n 'demo.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi

echo $ARGS

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -d)
            DEVICE=1
            shift
            ;;
        -o)
            base_out_path=$2
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

key_store_path=$basepath/../key_store_test
mkdir -p $base_out_path/temp

#generate get_device_state data
$basepath/provision_get_device_state.sh --tsm_private_key_path $key_store_path/tsm/sign_cert/key/private_key.pem --tsm_cert_path $key_store_path/tsm/sign_cert/tsm_sign.pem --tsm_ocsp_path $key_store_path/tsm/sign_cert/ocsp/tsm_sign_resp.pem --out_file_path $base_out_path/provision_get_device_state.json

#generate delete sd data
$basepath/provision_delete_sd.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sd --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/ha/sign_cert/ha_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/key/private_key.pem --out_file_path $base_out_path/provision_delete_sd.json --delete_ha "true"

rm -rf $base_out_path/temp
