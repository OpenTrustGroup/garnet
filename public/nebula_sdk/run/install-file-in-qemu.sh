#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# push a HA program into HEE of QEMU. NOT run.

set -e

function usage()
{
    printf '%s: <-e exe> <-o path> [-h]\n' "$0"
    printf '  -e <exe path> the program run in Linux in QEMU.\n'
    printf '  -o <dir>      the directory in Linux in QEMU.\n'
    printf '  -i <argument> SSH IP address when running qemu.\n'
    printf '  -a sship:(Optional)qemu ssh ip. Default:192.168.3.53\n'
    printf '  -h            show help\n'
}

while getopts "e:o:hi:a:" opt; do
  case "${opt}" in
    e)
      EXE=${OPTARG}
      ;;
    o)
      TO_DIR=${OPTARG}
      ;;
    a)
      SSH_IP=${OPTARG}
      ;;
    h) usage 1>&2 ; exit 0 ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done

if [ -z "$EXE" ]; then
    echo "Please set path of executable program."
    usage
    exit 1
fi

if [ ! -f "$EXE" ]; then
    echo "File \"$EXE\" does NOT exist."
    exit 1
fi

if [ -z "$TO_DIR" ]; then
    echo "Please set directory in Linux QEMU."
    usage
    exit 1
fi

EXE_NAME=`basename $EXE`

SSHD_IP_PARAM=root@192.168.3.53
if [ -n "$SSH_IP" ]; then
    SSHD_IP_PARAM=root@$SSH_IP
fi

#echo "copy file into Linux of QEMU.${SSHD_IP_PARAM}"
ssh -o StrictHostKeyChecking=no ${SSHD_IP_PARAM} "mkdir -p ${TO_DIR}"
echo -e "copy file $EXE into ${SSHD_IP_PARAM}:${TO_DIR}/${EXE_NAME}\n"
scp -o StrictHostKeyChecking=no $EXE ${SSHD_IP_PARAM}:${TO_DIR}/${EXE_NAME}
