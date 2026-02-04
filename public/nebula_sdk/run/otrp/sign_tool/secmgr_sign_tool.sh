#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
if [ -z "$secmgr_sign_tool_path" ]; then
    secmgr_sign_tool_path=$basepath/secmgr_sign_tool.jar
fi

USAGE='usage: Nebula secure manager sp sign ha tool.It is used to encrypt and sign an HA\n
         --ha_path ha image path.\n
         --sp_private_key_pem_path SP private key pem file path.\n
         --far_tool_file (Optional)Far tool path.Otherwise use the default path under the SDK.\n
         --spaik_public_key_path SPAIK public key path.It is used to encrypt ha.
         --out_file The full path and name of the output file.\n
         -h Show help.\n'

echo "start nebula sign tool"

ARGS=`getopt -o h --long ha_path:,sp_private_key_pem_path:,far_tool_file:,spaik_public_key_path:,out_file: -n 'secmgr_sign_tool.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        --ha_path)
            HA_PATH=$2;
            shift 2
            ;;
        --sp_private_key_pem_path)
            SP_PRIVATE_KEY_PATH=$2;
            shift 2
            ;;
        --far_tool_file)
            FAR_TOOL_FILE=$2;
            shift 2
            ;;
        --spaik_public_key_path)
            SPAIK_PUBLIC_KEY_PATH=$2;
            shift 2
            ;;
        --out_file)
            OUT_FILE=$2;
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

if [ ! -z "$HA_PATH" ]; then
    ha_path_param=" --ha_path "${HA_PATH}
else
    echo "ha_path is null"
    echo -e ${USAGE}
    exit 0
fi

if [ ! -z "$OUT_FILE" ]; then
    out_file_param=" --out_file "${OUT_FILE}
else
    echo "out_file is null"
    echo -e ${USAGE}
    exit 0
fi

if [ ! -z "$SP_PRIVATE_KEY_PATH" ]; then
    sp_private_key_path=" --sp_private_key_pem_path "${SP_PRIVATE_KEY_PATH}
else
    echo "sp private key path is null"
    echo -e ${USAGE}
    exit 0
fi

if [ ! -z "$SPAIK_PUBLIC_KEY_PATH" ]; then
    spaik_public_key_path=" --spaik_public_key_path "${SPAIK_PUBLIC_KEY_PATH}
else
    echo "spaik_public_key_path is null"
    echo -e ${USAGE}
    exit 0
fi

if [ ! -z "$FAR_TOOL_FILE" ]; then
    far_path_param=" --far_tool_file "${FAR_TOOL_FILE}
fi

    java -jar ${secmgr_sign_tool_path} sign_ha ${ha_path_param}${out_file_param}${sp_private_key_path}${spaik_public_key_path}${far_path_param}

echo "finish nebula sign tool"
