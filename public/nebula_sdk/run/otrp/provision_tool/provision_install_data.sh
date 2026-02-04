#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
SSHD_IP_DEFAULT=192.168.3.53
basepath=$(cd `dirname $0`; pwd)

USAGE='usage: Install the data from the already pushed device. \n
         The program will first install the provision_get_device_state.json, provision_delete_sd.json, provision_create_sd.json. \n
         and then the json data of the other install ha.\n
         --data_path Provision data folder\n
         --ssh_ip (Optional) SSH IP address when running qemu.The default is 192.168.3.53.\n
         -d (Optional)Run in the device when the "-d" parameter is set, otherwise run in QEMU.\n
         -h Show help.\n'

echo "start provision install data script"

ARGS=`getopt -o hd --long data_path:,ssh_ip: -n 'provision_install_data.sh' -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

while true
do
    case "$1" in
        -h) echo -e ${USAGE}; exit 0;;
        -d)
            DEVICE=1;
            shift
            ;;
        --data_path)
            DATA_PATH=$2;
            shift 2
            ;;
        --ssh_ip)
            SSH_IP=$2;
            shift 2
            ;;
        --)
            shift;
            break
            ;;
        *)echo -e ${USAGE}; exit 1;;
    esac
done

if [ -n "$SSH_IP" ]; then
    SSH_IP_PARAM="root@${SSH_IP}"
else
    SSH_IP_PARAM="root@${SSHD_IP_DEFAULT}"
fi

if [ -n "$DEVICE" ]; then
    adb shell nbl_daemon -p ${DATA_PATH}
else
    ssh -o StrictHostKeyChecking=no ${SSH_IP_PARAM} "nbl_daemon -p ${DATA_PATH}"
fi

echo "finish provision install data script"
