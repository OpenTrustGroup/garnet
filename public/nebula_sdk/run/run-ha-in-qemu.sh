#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# push a HA program into HEE of QEMU, and run.

set -e

RUN_ARG=

function usage()
{
    printf '%s: <-e/--exe exe> [-a/run_ha_param arg] [-h/--help]\n' "$0"
    printf '  -e/--exe <exe path>    the program run in Linux in QEMU.\n'
    printf '  -a/--run_ha_param arg  run with arg.\n'
    printf '  -t/--with_tracer       enable event tracer.\n'
    printf '  -i/--ssh_ip SSH IP address when running qemu.\n'
    printf '  -h/--help              show help\n'
}

# Use getopt to parse arguments.
ARGS=`getopt -o e:a:thi: --long exe:,run_ha_param:,with_tracer -n 'b.sh' -- "$@"`
# Reorder the arguments.
eval set -- "${ARGS}"

while true
do
    case "$1" in
        -e|--exe)
            EXE="$2"
            shift 2
            ;;
        -a|--run_ha_param)
            RUN_ARG="${RUN_ARG} --arg \"$2\"";
            shift 2
            ;;
        -t|--with_tracer)
            RUN_ARG="${RUN_ARG} --enable-tracer";
            shift 1
            ;;
        -i|--ssh_ip)
            SSH_IP="$2"
            shift 2
            ;;
        -h|--help)
            usage 1>&2; exit 0;;
        --)
            shift; break;;
        *)
            usage 1>&2 ; exit 1;;
    esac
done

while getopts "e:ha:" opt; do
  case "${opt}" in
    e)
      EXE=${OPTARG}
      ;;
    a)
      RUN_ARG="--arg \"${OPTARG}\""
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

echo "copy file into Linux of QEMU."
ssh -o StrictHostKeyChecking=no ${SSHD_IP} "mkdir -p /root/hee"
scp -o StrictHostKeyChecking=no $EXE ${SSHD_IP}:/root/hee/$EXE_NAME

echo "Run exe in HEE of QEMU. The log is in HEE Terminal or REE Terminal."
set +e
ssh -o StrictHostKeyChecking=no ${SSHD_IP} "nblruncmd killall $EXE_NAME"
ssh -o StrictHostKeyChecking=no ${SSHD_IP} "nbl_daemon --load-in-non-secure /root/hee/${EXE_NAME} ${RUN_ARG}"
