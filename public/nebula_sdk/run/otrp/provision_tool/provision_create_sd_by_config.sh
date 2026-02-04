#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)

function help()
{
    echo "Generate create and update security domain data from the configuration file."
    echo "-c Provision config folder path."
    echo "--sd_privilege_file file path whose formart is JSON contains privileges of this SD.\n"
    echo "-o Output path."
    echo "-h show help."
}

# parse the arguments of scripts
ARGS=`getopt -o hc:o: --long help:,sd_privilege_file: -n "$0" -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -c)
            config_folder_path=$2
            shift 2
            ;;
        --sd_privilege_file)
	    SD_PRIVILEGE_FILE=$2
            shift 2
            ;;
        -o)
            base_out_path=$2
            shift 2
            ;;
        --help)
            help
            exit 0
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
    echo "Generate provision data failed. Output param (-o) is empty."
    exit 1
fi

if [ -z "$config_folder_path" ]; then
    echo "Generate provision data failed. Config folder path param (-c) is empty."
    exit 1
fi

if [ -n "$SD_PRIVILEGE_FILE" ]; then
    SD_PRIVILEGE_FILE_PARAM="--privilege_file $SD_PRIVILEGE_FILE"
else
    echo "privilege_file is null."
    exit 1
fi


array=(${config_folder_path//,/ })
save_path_array=()
count=0
for config_folder_path_item in ${array[@]}
do
    echo "Start create provision ha for folder: "$config_folder_path_item

    #Check if the same output path exists
    for i in ${save_path_array[@]}
    do
        if [ "$i" == "$config_folder_path_item" ]; then
            echo "[ERROR] Configuration folder duplicate name. It will cause the output folder to already exist."
            exit 1
        fi
    done
    out_last_folder=${config_folder_path_item##*/}
    save_path_array[count]=$out_last_folder
    count+=1

    mkdir -p $base_out_path/$out_last_folder

    source $config_folder_path_item/provision.config
    provision_key_base_path=$config_folder_path_item
    #create data

    tsm_private_key_path=$provision_key_base_path/tsm/sign_cert/private_key.pem
    tsm_cert_path=$provision_key_base_path/tsm/sign_cert/tsm_sign.pem
    tsm_ocsp_path=$provision_key_base_path/tsm/sign_cert/ocsp/tsm_sign_resp.pem
    sp_cert_path=$provision_key_base_path/sp/sign_cert/sp_sign.pem
    hee_cert_path=$provision_key_base_path/hee/sign_cert/hee_sign.pem
    sp_private_key_pem_path=$provision_key_base_path/sp/sign_cert/private_key.pem
    spaik_public_key_path=$provision_key_base_path/spaik/spaik_public_key.pem

    #generate get_device_state data
    $basepath/provision_get_device_state.sh --tsm_private_key_path $tsm_private_key_path --tsm_cert_path $tsm_cert_path --tsm_ocsp_path $tsm_ocsp_path --out_file_path $base_out_path/$out_last_folder/provision_get_device_state.json

    #generate create sd data
    $basepath/provision_create_sd.sh --tsm_id $tsm_id --sp_id $sp_id --hee_name $hee_name --sd_name $sd_name --sp_cert_path $sp_cert_path --hee_cert_path $hee_cert_path --tsm_private_key_path $tsm_private_key_path --out_file_path $base_out_path/$out_last_folder/provision_create_sd.json $SD_PRIVILEGE_FILE_PARAM

    #generate update sd data
    $basepath/provision_update_sd.sh --tsm_id $tsm_id --sp_id $sp_id --hee_name $hee_name --sd_name $sd_name --hee_cert_path $hee_cert_path --tsm_private_key_path $tsm_private_key_path --out_file_path $base_out_path/$out_last_folder/provision_update_sd.json $SD_PRIVILEGE_FILE_PARAM
done
