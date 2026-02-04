#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail; [[ "$TRACE" ]] && set -x

basepath=$(cd `dirname $0`; pwd)
if [ -z "$secmgr_provision_tool_path" ]; then
    secmgr_provision_tool_path=$basepath/secmgr_provision_tool.jar
fi

USAGE='usage: Generate a data file that get device state\n
         --tsm_private_key_path tsm private key pem path, it is used to sign the message data.\n
         --tsm_cert_path tsm cert path.This certificate will be sent to HEE. HEE will use this certificate to encrypt the returned data.\n
         --tsm_ocsp_path TSM OCSP data, used to validate the expiration of the CA certificate.\n
         --out_file_path The full path to the output file needs to include the file name. The file name must be "provision_get_device_state.json"\n
         -h Show help.\n'

echo "start provision get device state script"

ARGS=`getopt -o h --long tsm_private_key_path:,tsm_cert_path:,tsm_ocsp_path:,out_file_path: -n 'provision_get_device_state.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        --tsm_private_key_path)
            TSM_PRIVATE_PATH=$2;
            shift 2
            ;;
        --tsm_cert_path)
            TSM_CERT_PATH=$2;
            shift 2
            ;;
        --tsm_ocsp_path)
            TSM_OCSP_PATH=$2;
            shift 2
            ;;
        --sp_cert_path)
            SP_CERT_PATH=$2;
            shift 2
            ;;
        --out_file_path)
            OUT_FILE_PATH=$2;
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

if [ -n "$TSM_PRIVATE_PATH" ]; then
    tsm_private_key_path_param=" --tsm_private_key_path "${TSM_PRIVATE_PATH}
else
    echo "Tsm private key path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$TSM_CERT_PATH" ]; then
    tsm_cert_path_param=" --tsm_cert_path "${TSM_CERT_PATH}
else
    echo "Tsm ocsp path is null."
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$TSM_OCSP_PATH" ]; then
    tsm_ocsp_param=" --tsm_ocsp_path "${TSM_OCSP_PATH}
else
    echo "out_file is null"
    echo -e ${USAGE}
    exit 0
fi

if [ -n "$OUT_FILE_PATH" ]; then
    out_file_path_param=" --out_file_path "${OUT_FILE_PATH}
else
    echo "Out file path is null."
    echo -e ${USAGE}
    exit 0
fi

java -jar $secmgr_provision_tool_path get_device_state ${tsm_private_key_path_param}  ${tsm_cert_path_param} ${tsm_ocsp_param} ${out_file_path_param}

echo "finish provision get device state script"
