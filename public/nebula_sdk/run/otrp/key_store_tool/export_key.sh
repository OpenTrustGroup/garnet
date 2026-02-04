#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#Export the key to be injected from the key folder

USAGE='usage: Export the key to be injected from the key folder.\n
         -k Key folder path. Need to be set when -a is set.\n
         -o output directory. Need to be set when -a is set.\n
         -a (Optional) Perform export of the key certificate. Otherwise only provide functions.\n
         -h Show help.\n'

echo "start export_key.sh"

# parse the arguments of scripts
ARGS=`getopt -o hao:k: -n 'export_key.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Arguments are error."
    exit 1
fi
#echo $ARGS

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -k)
            in_key_folder_path=$2
            shift 2
            ;;
        -o)
            base_out_path=$2
            shift 2
            ;;
	-a)
            export_all=1
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

export_cert(){
if [ -z "$1" ]; then
    echo "Export cert failed. File: $1 not exist."
    exit 1
fi

if [ -z "$2" ]; then
    echo "Export cert failed. Out path null."
    exit 1
fi

    cert_data=$(sed -n '/^-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----$/p' $1 | grep -Ev '(^-----BEGIN CERTIFICATE-----|-----END CERTIFICATE-----$)' | cut -f 1,2)
if [ -z "$cert_data" ]; then
    echo "Export cert failed. The file contents may not be properly formatted."
    exit 1
fi

rm -rf $2
echo $cert_data | sed 's/ //g'> $2 #remove space and export file

}

export_private_key(){
if [ -z "$1" ]; then
    echo "Export key failed. File: $1 not exist."
    exit 1
fi

if [ -z "$2" ]; then
    echo "Export key failed. Out path null."
    exit 1
fi

rm -rf $2
cp $1 $2

}

if [ -n "$export_all" ]; then

    if [ -z "$in_key_folder_path" ]; then
        echo "-k parameter cannot be empty"
        echo "$in_key_folder_path"
        echo -e ${USAGE}
        exit 1
    fi

    if [ -z "$base_out_path" ]; then
        echo "-o parameter cannot be empty"
        echo -e ${USAGE}
        exit 1
    fi

    mkdir -p $base_out_path

    echo "export hee second ca cert start."
    export_cert ${in_key_folder_path}/hee/second_ca/hee_second_ca.crt $base_out_path/hee_ca_cert
    echo "export hee second ca cert finish."

    echo "export hee cert start."
    export_cert ${in_key_folder_path}/hee/sign_cert/hee_sign.crt $base_out_path/hee_cert
    echo "export hee cert finish."

    echo "export hee private key start."
    export_private_key ${in_key_folder_path}/hee/sign_cert/private_key.pem $base_out_path/hee_private_key
    echo "export hee private key finish."

    echo "export spaik json start."
    export_private_key ${in_key_folder_path}/spaik/spaik.json $base_out_path/spaik_provision
    echo "export spaik json finish."

    echo "export tfw cert start."
    export_cert ${in_key_folder_path}/tfw/sign_cert/tfw_sign.crt $base_out_path/tfw_cert
    echo "export tfw cert finish."

    echo "export tfw private key."
    export_private_key ${in_key_folder_path}/tfw/sign_cert/private_key.pem $base_out_path/tfw_private_key
    echo "export tfw private key finish."

    echo "export tsm root cert."
    export_cert ${in_key_folder_path}/tsm/root_ca/tsm_root_ca.crt $base_out_path/tsm_root_cert
    echo "export tsm root cert finish."
fi

echo "export_key.sh finish"
