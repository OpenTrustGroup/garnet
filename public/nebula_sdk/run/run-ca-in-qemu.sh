#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# push a CA program into REE of QEMU, and run it.

set -e

function usage()
{
    printf '%s: <-e exe> [-a arg] [-h]\n' "$0"
    printf '  -e <exe path> the program run in Linux in QEMU.\n'
    printf '  -a <argument> the argument for exe.\n'
    printf '  -i <argument> SSH IP address when running qemu.\n'
    printf '  -h            show help\n'
}

while getopts "e:a:hi:" opt; do
  case "${opt}" in
    e)
      EXE=${OPTARG}
      ;;
    a)
      ARG=${OPTARG}
      ;;
    -i)
      SSH_IP="$2"
      shift 2
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

EXE_NAME=`basename $EXE`

SSHD_IP=root@192.168.3.53
if [ -n "$SSH_IP" ]; then
    SSHD_IP=root@$SSH_IP
fi

echo "copy file into Linux in QEMU."
ssh -o StrictHostKeyChecking=no ${SSHD_IP} "mkdir -p /root/ree"
scp -o StrictHostKeyChecking=no $EXE ${SSHD_IP}:/root/ree/$EXE_NAME

echo "Run exe in Linux in QEMU. The log is in HEE Terminal."
ssh -o StrictHostKeyChecking=no ${SSHD_IP} "/root/ree/${EXE_NAME} ${ARG}"
