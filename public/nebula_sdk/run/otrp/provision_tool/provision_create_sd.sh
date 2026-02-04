#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail; [[ "$TRACE" ]] && set -x

basepath=$(cd `dirname $0`; pwd)
if [ -z "$secmgr_provision_tool_path" ]; then
    secmgr_provision_tool_path=$basepath/secmgr_provision_tool.jar
fi

USAGE='usage: Generate a data file that creates a secure domain\n
         --tsm_id TSM/TAM identifier.\n
         --sp_id Service Provider identifier.\n
         --hee_name HEE name.\n
         --sd_name Security domain name.\n
         --sp_cert_path SP cert path. It is used to verify the signature of the HA.\n
         --hee_cert_path HEE cert path, it is used to encrypt the message data.\n
         --tsm_private_key_path tsm private key pem path, it is used to sign the message data.\n
         --privilege_file [optional] file path whose formart is JSON contains privileges of this SD.\n
         --out_file_path The full path to the output file needs to include the file name. The file name must be "provision_create_sd.json"\n
         -h Show help.\n'

echo "start provision create sd script"

mkdir -p $basepath/out

ARGS=`getopt -o h --long tsm_id:,sp_id:,hee_name:,sd_name:,sp_cert_path:,hee_cert_path:,tsm_private_key_path:,out_file_path:,privilege_file: -n 'provision_create_sd.sh' -- "$@"`
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
        --privilege_file)
            sd_privilege_file=$2;
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

# privilege is OPTIONAL.
if [ -n "$sd_privilege_file" ]; then
    if [ ! -f ${sd_privilege_file} ]; then
        echo "Privilege file \"${sd_privilege_file}\" does NOT exist."
        exit 1
    fi
    sd_privilege_file_param=" --privilege_file "${sd_privilege_file}
fi

java -jar $secmgr_provision_tool_path create_sd ${tsm_id_param} ${sp_id_param} ${hee_name_param}  ${sd_name_param} ${sp_cert_path_param} ${hee_cert_path_param} ${tsm_private_key_path_param} ${sd_privilege_file_param} ${out_file_path_param}

echo "finish provision create sd script"
