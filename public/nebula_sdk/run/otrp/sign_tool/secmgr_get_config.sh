#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
tool_path=$basepath/secmgr_sign_tool.jar

USAGE='usage: Get the JSON data of the manifest configuration from the json file of the encrypted and signed HA\n
         --enc_ha_path Encrypted HA file path.\n
         --out_file The full path and name of the output file.\n
         -h Show help.\n'

echo "start nebula get config tool"

ARGS=`getopt -o h --long enc_ha_path:,out_file: -n 'secmgr_get_config.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        --enc_ha_path)
            ENC_HA_PATH=$2;
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

if [ ! -z "$ENC_HA_PATH" ]; then
    ha_path_param=" --enc_ha_path "${ENC_HA_PATH}
else
    echo "enc_ha_path is null"
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

    java -jar ${tool_path} get_config ${ha_path_param}${out_file_param}

echo "finish nebula get config tool"
