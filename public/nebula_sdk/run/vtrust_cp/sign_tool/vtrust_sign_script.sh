#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
if [ -z "$sign_tool_path" ]; then
    sign_tool_path=$basepath/../../otrp/sign_tool/secmgr_sign_tool.jar
fi

USAGE='usage: sign_data:signature data with sha256\n
         --data_path data path.\n
         --private_key_pem_path private key pem file path.\n
         --out_file The full path and name of the output file.\n
         -h Show help.\n'

echo "start vtrust sign data tool"

ARGS=`getopt -o h --long data_path:,private_key_pem_path:,out_file: -n 'vtrust_sign_script.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        --data_path)
            DATA_PATH=$2;
            shift 2
            ;;
        --private_key_pem_path)
            PRIVATE_KEY_PATH=$2;
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

if [ ! -z "$DATA_PATH" ]; then
    data_path_param=" --data_path "${DATA_PATH}
else
    echo "data_path is null"
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

if [ ! -z "$PRIVATE_KEY_PATH" ]; then
    private_key_path=" --private_key_path "${PRIVATE_KEY_PATH}
else
    echo "private key path is null"
    echo -e ${USAGE}
    exit 0
fi
    java -jar ${sign_tool_path} sign_data ${data_path_param}${out_file_param}${private_key_path}

echo "finish vtrust sign data tool"
