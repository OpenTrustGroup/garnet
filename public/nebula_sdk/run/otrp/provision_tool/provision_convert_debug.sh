#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
if [ -z "$secmgr_provision_tool_path" ]; then
    secmgr_provision_tool_path=$basepath/secmgr_provision_tool.jar
fi

USAGE='usage: Convert provision data to debug provision data\n
         --in_file_path Provision install HA json file path.\n
         --spaik_private_key_pem_path Spaik private pem pkcs8 key path.\n
         --out_file_folder Output debug provision data folder.\n
         -h Show help.\n'

echo "start provision convert debug script"

mkdir -p $basepath/out

ARGS=`getopt -o h --long in_file_path:,spaik_private_key_pem_path:,out_file_folder: -n 'provision_convert_debug.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        --in_file_path)
            in_file_path=$2;
            shift 2
            ;;
        --spaik_private_key_pem_path)
            spaik_private_key_pem_path=$2;
            shift 2
            ;;
        --out_file_folder)
            out_file_folder=$2;
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

if [ -n "$in_file_path" ]; then
    in_file_path_param=" --in_file_path "${in_file_path}
else
    echo "In file path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$spaik_private_key_pem_path" ]; then
    spaik_private_key_path_param=" --spaik_private_key_pem_path "${spaik_private_key_pem_path}
else
    echo "TSM private key path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$out_file_folder" ]; then
    out_file_path_param=" --out_file_path "${out_file_folder}
else
    echo "Out file path is null."
    echo -e ${USAGE}
    exit 0
fi

$basepath/provision_get_ha.sh ${in_file_path_param} ${spaik_private_key_path_param} ${out_file_path_param}/ha
$basepath/provision_create_ha.sh -e ${out_file_folder}/ha -o ${out_file_folder}

echo "finish provision convert debug script"
