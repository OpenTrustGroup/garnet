#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail; [[ "$TRACE" ]] && set -x

SSH_IP=192.168.3.53

basepath=$(cd `dirname $0`; pwd)

function help()
{
    echo "Generate provision data from the configuration file."
    echo "-c Provision config folder path."
    echo "-e Ha image path or encrypted HA json data."
    echo "-o Output path."
    echo "-q <ssh ip>(Optional) Run on qemu. The parameter value is ssh_ip. When the parameter is set to \"\" ssh ip is ${SSH_IP}"
    echo "-d (Optional) Run on device by adb. Only one of parameter -q and -d can be selected"
    echo "--run_ha_param <ARGS> (Optional) Parameters used to run HA/TA"
    echo "-h show help."
}

# parse the arguments of scripts
ARGS=`getopt -o hdc:o:e:q: --long sd_privilege_file:,run_ha_param: -n "$0" -- "$@"`
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
        -e)
            ha_path=$2
            shift 2
            ;;
        -o)
            base_out_path=$2
            shift 2
            ;;
        -q)
            debug_qemu=1
            debug_qemu_sship=$2
            shift 2
            ;;
        -d)
            debug_device=1
            shift
            ;;
        --sd_privilege_file)
            SD_PRIVILEGE_FILE=$2
            shift 2
            ;;
        --run_ha_param)
            RUN_ARG="-a \"$2\""
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
    echo "Generate provision data failed. Output param (-o) is empty."
    exit 1
fi

if [ -z "$config_folder_path" ]; then
    echo "Generate provision data failed. Config folder path param (-c) is empty."
    exit 1
fi

if [ -z "$ha_path" ]; then
    echo "Generate provision data failed. HA path param (-e) is empty."
    exit 1
fi

if [ -n "$SD_PRIVILEGE_FILE" ]; then
	SD_PRIVILEGE_FILE_PARAM="--privilege_file $SD_PRIVILEGE_FILE"
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
    mkdir -p $base_out_path/delete_sd/$out_last_folder
    mkdir -p $base_out_path/temp

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
    $basepath/provision_create_sd.sh --tsm_id $tsm_id --sp_id $sp_id --hee_name $hee_name --sd_name $sd_name --sp_cert_path $sp_cert_path --hee_cert_path $hee_cert_path --tsm_private_key_path $tsm_private_key_path --out_file_path $base_out_path/$out_last_folder/provision_create_sd.json ${SD_PRIVILEGE_FILE_PARAM}

    #generate update sd data
    $basepath/provision_update_sd.sh --tsm_id $tsm_id --sp_id $sp_id --hee_name $hee_name --sd_name $sd_name --hee_cert_path $hee_cert_path --tsm_private_key_path $tsm_private_key_path --out_file_path $base_out_path/$out_last_folder/provision_update_sd.json ${SD_PRIVILEGE_FILE_PARAM}

    #generate delete sd data
    $basepath/provision_delete_sd.sh --tsm_id $tsm_id --sp_id $sp_id --hee_name $hee_name --sd_name $sd_name --sp_cert_path $sp_cert_path --hee_cert_path     $hee_cert_path --tsm_private_key_path $tsm_private_key_path --out_file_path $base_out_path/delete_sd/$out_last_folder/provision_delete_sd.json --delete_ha "true"

    #check sign ha
    ha_file_name=${ha_path##*/}
    ha_name=${ha_file_name%.*}

    signed_exe_path=$ha_path

    ha_data_demo=$(cut -b1-13 $ha_path)

    if [ "$ha_data_demo" = "{\"protected\":" ]; then
        echo "It is encrypted ha."
    else
        echo "It is Unencrypted ha."
    #encrypt ha
    signed_exe_path=$base_out_path/$out_last_folder/$ha_name.enc

    $basepath/../sign_tool/secmgr_sign_tool.sh --far_tool_file $basepath/../elf_tool/far --ha_path ${ha_path} --sp_private_key_pem_path $sp_private_key_pem_path --spaik_public_key_path $spaik_public_key_path --out_file $signed_exe_path
    fi

    #generate install ha data
    $basepath/provision_install_ha.sh --tsm_id $tsm_id --sp_id $sp_id --hee_name $hee_name --sd_name $sd_name --sp_cert_path $sp_cert_path --hee_cert_path $hee_cert_path --tsm_private_key_path $tsm_private_key_path --out_file_path $base_out_path/$out_last_folder/provision_install_$ha_name.json --ha_path ${signed_exe_path}

    #install app
    if [ -n "$debug_qemu" ] || [ -n "$debug_device" ]; then
        #get app uuid
        rm -f $base_out_path/temp/ha_config.txt

        $basepath/../sign_tool/secmgr_get_config.sh --enc_ha_path ${signed_exe_path} --out_file ${base_out_path}/temp/ha_config.txt
        if [ ! -f "${base_out_path}/temp/ha_config.txt" ];then
            echo "Get HA/TA uuid failed. Unable to run TA/HA"
                if [ -f "$base_out_path/$out_last_folder/$ha_name.enc" ];then
                    rm -f $base_out_path/$out_last_folder/$ha_name.enc
                fi
            exit 1
        fi
        uuid=$(cat ${base_out_path}/temp/ha_config.txt | sed 's/,/\n/g' | grep "uuid" | sed 's/:/\n/g' | sed '/^$/!h;$!d;g' | sed 's/}//g' | sed 's/"//g')
        if [ -z "$uuid" ];then
            echo "Parse json HA/TA uuid failed."
            exit 1
        fi

        if [ -f "$base_out_path/$out_last_folder/$ha_name.enc" ];then
            rm -f $base_out_path/$out_last_folder/$ha_name.enc
        fi

        #push data and run
        if [ -n "$debug_qemu" ];then
            echo "Push provision data to qemu."
            run_ssh_ip=${SSH_IP}
            if [ -n "$debug_qemu_sship" ]; then
                run_ssh_ip=$debug_qemu_sship
            fi
            ssh -o StrictHostKeyChecking=no root@$run_ssh_ip rm -rf /root/ree/secmgr/secmgr_provision_debug/*
            $basepath/push_qemu.sh $base_out_path/$out_last_folder /root/ree/secmgr/secmgr_provision_debug $run_ssh_ip
            $basepath/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_provision_debug --ssh_ip $run_ssh_ip
            echo "Push provision data to qemu finish."
            #run app qemu
            echo "Launch app ${uuid}"
            ssh -o StrictHostKeyChecking=no root@$run_ssh_ip nbl_daemon -l ${uuid} ${RUN_ARG}
        else
            echo "Push provision data to device."
            adb root
            adb remount
            adb shell mkdir -p /data/nebula/secmgr/secmgr_provision_debug
            adb shell rm -rf /data/nebula/secmgr/secmgr_provision_debug/*
            $basepath/push_android.sh $base_out_path/$out_last_folder /data/nebula/secmgr/secmgr_provision_debug
            adb shell nbl_daemon -p /data/nebula/secmgr/secmgr_provision_debug
            echo "Push provision data to device finish."
            #run app device
            echo "Launch app ${uuid}"
            adb shell nbl_daemon -l ${uuid} ${RUN_ARG}
        fi
    else
        rm -f $base_out_path/$out_last_folder/$ha_name.enc
    fi
done
