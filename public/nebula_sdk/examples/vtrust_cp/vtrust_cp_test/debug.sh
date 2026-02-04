#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function show_help()
{
    echo "debug vtrust cross platform app"
    echo "-d Run in the device when the '-d' parameter is set, otherwise run in QEMU."
    echo "-h show help."
}

basepath=$(cd `dirname $0`; pwd)
sdk_path=$basepath/../../../

# parse the arguments of scripts
ARGS=`getopt -o dh --long help -n 'debug.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -d)
            DEVICE=1
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -h)
            show_help
            exit 0
            ;;
        *)
            show_help
            exit 1
            ;;
    esac
done

#sign data
$sdk_path/run/vtrust_cp/sign_tool/vtrust_sign_script.sh --data_path $basepath/data/script.py --private_key_pem_path $sdk_path/run/vtrust_cp/key_tool/debug_key/sign/key_sign_privatekey.pem --out_file $basepath/data/script_sign

#create & run ta
echo "create &run ta."
if [ -n "$DEVICE" ]; then
    $sdk_path/run/otrp/provision_tool/provision_create_ha_by_config.sh -c $sdk_path/run/otrp/key_store_test -e $basepath/ta_image/vtrust_cp_ta -o $basepath/ta_json -d
else
    $sdk_path/run/otrp/provision_tool/provision_create_ha_by_config.sh -c $sdk_path/run/otrp/key_store_test -e $basepath/ta_image/vtrust_cp_ta -o $basepath/ta_json -q ""
fi

#data to c header
mkdir -p $basepath/data/include
rm -rf $basepath/data/include/*
cd $basepath/data/
xxd -i script_sign >> $basepath/data/include/script_sign.h
xxd -i sign_publickey_data >> $basepath/data/include/sign_publickey_data.h
xxd -i script.py >> $basepath/data/include/script_py.h
cd ../

#push_data
#echo "push data."
#if [ -n "$DEVICE" ]; then
#    $sdk_path/run/otrp/provision_tool/push_android.sh $basepath/data /data/nebula/vtrust_cp_debug
#else
#    $sdk_path/run/otrp/provision_tool/push_qemu.sh $basepath/data /root/ree/vtrust_cp_debug
#fi

echo "run ca."
#run ca
if [ -n "$DEVICE" ]; then
    make
    make run-ndk
else
    make
    make run
fi
