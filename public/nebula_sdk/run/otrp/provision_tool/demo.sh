#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function help()
{
    echo "demo.sh [-ehdo]"
    echo "-e set path of executable file."
    echo "--delete_sd [optional]delete sd."
    echo "--privilege_file [optional] file path whose formart is JSON contains privileges of this SD."
    echo "-d Run in the device when the '-d' parameter is set, otherwise run in QEMU."
    echo "-o <output directory> if not set, directory is $basepath/out."
    echo "-a sship:(Optional)qemu ssh ip. Default:192.168.3.53\n"
    echo "-h show help."
}

basepath=$(cd `dirname $0`; pwd)
base_out_path=$basepath/out
nbl_daemon_path=$basepath/../nbl_daemon
device_secmgr_path=/data/nebula/secmgr

SD_NAME=debug_sd
exe_path=$basepath/../../../examples/hee/libc/out/test-libc
signed_exe_path=$base_out_path/temp/signed_ha

# parse the arguments of scripts
ARGS=`getopt -o hdo:e:a: --long privilege_file:,delete_sd -n 'demo.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi
#echo $ARGS

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
        -e)
            exe_path=$2
            shift 2
            ;;
        -a)
            ssh_ip=$2
            shift 2
            ;;
        --privilege_file)
            sd_privilege_file=$2;
            shift 2
            ;;
        --delete_sd)
            delete_sd=y
            shift
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

# privilege is OPTIONAL.
if [ -n "$sd_privilege_file" ]; then
    if [ ! -f ${sd_privilege_file} ]; then
        echo "Privilege file \"${sd_privilege_file}\" does NOT exist."
        exit 1
    fi
    sd_privilege_file_param=" --privilege_file "${sd_privilege_file}
fi

key_store_path=$basepath/../key_store_test
mkdir -p $base_out_path/temp
signed_exe_path=$base_out_path/temp/signed_ha

#generate get_device_state data
$basepath/provision_get_device_state.sh --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --tsm_cert_path $key_store_path/tsm/sign_cert/tsm_sign.pem --tsm_ocsp_path $key_store_path/tsm/sign_cert/ocsp/tsm_sign_resp.pem --out_file_path $base_out_path/provision_get_device_state.json

#generate delete sd data
if [ "$delete_sd" == 'y' ]; then
    $basepath/provision_delete_sd.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name ${SD_NAME} --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_delete_sd.json --delete_ha "true"
fi

#generate create sd data
$basepath/provision_create_sd.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name ${SD_NAME} --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem ${sd_privilege_file_param} --out_file_path $base_out_path/provision_create_sd.json

#demo
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --far_tool_file $basepath/../elf_tool/far --ha_path ${exe_path} --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $signed_exe_path
#generate install ha data
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name ${SD_NAME} --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_test-lib.json --ha_path ${signed_exe_path}
rm -rf $base_out_path/temp

#push data to device/qemu
if [ -n "$DEVICE" ]; then
    $basepath/push_android.sh $base_out_path $device_secmgr_path/secmgr_demo
else
    if [ -n "$ssh_ip" ]; then
        $basepath/push_qemu.sh $base_out_path /root/ree/secmgr/secmgr_demo $ssh_ip
    else
        $basepath/push_qemu.sh $base_out_path /root/ree/secmgr/secmgr_demo
    fi
fi

#install data

if [ -n "$DEVICE" ]; then
    $basepath/provision_install_data.sh --data_path /data/nebula/secmgr/secmgr_demo -d
else
    if [ -n "$ssh_ip" ]; then
        $basepath/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_demo --ssh_ip $ssh_ip
    else
        $basepath/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_demo
    fi
fi
