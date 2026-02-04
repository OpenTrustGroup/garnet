#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
if [ -z "$secmgr_provision_tool_path" ]; then
    secmgr_provision_tool_path=$basepath/secmgr_provision_tool.jar
fi

USAGE='usage: Generate a data file that install ha\n
         --tsm_id TSM/TAM identifier.\n
         --sp_id Service Provider identifier.\n
         --hee_name HEE name.\n
         --sd_name Security Domain name.\n
         --sp_cert_path SP cert path. It is used to verify the signature of the HA.\n
         --hee_cert_path HEE cert path, it is used to encrypt the message data.\n
         --tsm_private_key_path tsm private key pem path, it is used to sign the message data.\n
         --out_file_path out file path.\n
         --ha_id HA identifier (optional).If this parameter is empty, the id will be obtained from the manifest of the HA image. If there is no such option in the manifest, an error will be reported.
         --ha_path Path of the enc HA json data.
         -h Show help.\n'

echo "start provision install ha script"

mkdir -p $basepath/out

ARGS=`getopt -o h --long tsm_id:,sp_id:,hee_name:,sd_name:,sp_cert_path:,hee_cert_path:,tsm_private_key_path:,out_file_path:,ha_id:,ha_path: -n 'provision_install_ha.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        --tsm_id)
            tsm_id=$2;
            shift 2
            ;;
        --sp_id)
            sp_id=$2;
            shift 2
            ;;
        --hee_name)
            hee_name=$2;
            shift 2
            ;;
        --sd_name)
            sd_name=$2;
            shift 2
            ;;
        --sp_cert_path)
            sp_cert_path=$2;
            shift 2
            ;;
        --hee_cert_path)
            hee_cert_path=$2;
            shift 2
            ;;
        --tsm_private_key_path)
            tsm_private_key_path=$2;
            shift 2
            ;;
        --out_file_path)
            out_file_path=$2;
            shift 2
            ;;
        --ha_id)
            ha_id=$2;
            shift 2
            ;;
        --ha_path)
            ha_path=$2;
            shift 2
            ;;
        --)
            shift;
            break
            ;;
        *) echo -e ${USAGE}; exit 1;;
    esac
done

param=""

if [ -n "$tsm_id" ]; then
    tsm_id_param=" --tsm_id "${tsm_id}
else
    echo "Tsm id is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$sp_id" ]; then
    sp_id_param=" --sp_id "${sp_id}
else
    echo "SP id is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$hee_name" ]; then
    hee_name_param=" --hee_name "${hee_name}
else
    echo "Hee name is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$sd_name" ]; then
    sd_name_param=" --sd_name "${sd_name}
else
    echo "SD name is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$sp_cert_path" ]; then
    sp_cert_path_param=" --sp_cert_path "${sp_cert_path}
else
    echo "SP cert path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$hee_cert_path" ]; then
    hee_cert_path_param=" --hee_cert_path "${hee_cert_path}
else
    echo "HEE cert is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$tsm_private_key_path" ]; then
    tsm_private_key_path_param=" --tsm_private_key_path "${tsm_private_key_path}
else
    echo "TSM private key path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$out_file_path" ]; then
    out_file_path_param=" --out_file_path "${out_file_path}
else
    echo "Out file path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$ha_id" ]; then
    ha_id_param=" --ha_id "${ha_id}
fi

if [ -n "$ha_path" ]; then
    ha_path_param=" --ha_path "${ha_path}
else
    echo "HA path is null."
    echo -e ${USAGE}
    exit 0
fi

java -jar $secmgr_provision_tool_path install_ha ${tsm_id_param} ${sp_id_param} ${hee_name_param} ${sd_name_param} ${sp_cert_path_param} ${hee_cert_path_param} ${tsm_private_key_path_param} ${out_file_path_param} ${ha_id_param} ${ha_path_param}

echo "finish provision install ha script"
