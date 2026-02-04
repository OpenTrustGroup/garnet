#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### install all tests in SDK/test_suite
### - tests in SDK/test_suite are collected from /system/test in "debug" build.

set -e; [[ "${TRACE}" ]] && set -x

declare EXE_NAME=

function usage() {
  printf '%s: [-h/l]\n' "$0"
  printf '  -e name : execute this exe with the given name in test_suite.\n'
  printf '  -i haid : set customized ha id, default it is uuid.\n'
  printf '  -a sship:(Optional)qemu ssh ip. Default:192.168.3.53\n'
  printf '  -d      : Running on a real device.\n'
  printf '  -h      : show help.\n'
}

while getopts "e:i:ha:d" opt; do
  case "${opt}" in
    e)
      EXE_NAME=${OPTARG}
      ;;
    i)
      HAID=${OPTARG}
      ;;
    a)
      SSH_IP=${OPTARG}
      ;;
    d)
      RUN_DEVICE=1
      ;;
    h) usage 1>&2 ; exit 0 ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done

# get the path of this command, and then get the path of sdk.
# - CMD_DIR should be "<sdk>/run/"
CMD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SDK_HOME=$(dirname $CMD_DIR)
BASE_PATH=$SDK_HOME/run/otrp/debug_tool
TEST_SUITE=$SDK_HOME/test_suite
TEST_SD_NAME="debug_sd"

# find the qemu directory.
# The source of binary executable qemu is in "//third_party/qemu".
if [ -n "$FUCHSIA_DIR" ]; then # In Fuchsia env.
    echo "in nebula"
else # in SDK
    echo "in sdk."
fi

if [ -n "$SSH_IP" ]; then
	SSH_IP_PARAM=" --ssh_ip ${SSH_IP}"
fi

if [ -n "$RUN_DEVICE" ]; then
	RUN_DEVICE_PARAM=" -d"
fi

mkdir -p ${FUCHSIA_OUT_DIR}/testfile
temp_folder=$(mktemp -d ${FUCHSIA_OUT_DIR}/testfile/temp.XXXXXX)

function install_sd()
{
    ${BASE_PATH}/secmgr_init.sh ${SSH_IP_PARAM} --temp_folder $temp_folder $RUN_DEVICE_PARAM -v
    ${BASE_PATH}/secmgr_create_sd.sh -f --sd_name $TEST_SD_NAME -l ${SSH_IP_PARAM} --temp_folder $temp_folder $RUN_DEVICE_PARAM -v
}

function install_ha()
{
    if [ -z "$HAID" ]; then
        EXE_PATH_IN_IMAGE=`echo $1 | sed "s/.*test_suite//g"`
        HAID=`${CMD_DIR}/get_haid_by_str.sh $EXE_PATH_IN_IMAGE`
    fi
    ${BASE_PATH}/secmgr_install_ha.sh -f --sd_name $TEST_SD_NAME --ha_id $HAID --ha_path $1 -l ${SSH_IP_PARAM} --temp_folder $temp_folder $RUN_DEVICE_PARAM -v

    #echo "$1 : $HAID"
}

# At first, should create and install the Secure Domain.
install_sd

# if execute the given exe.
if [ -n "$EXE_NAME" ]; then
    EXE_PATH=`find $TEST_SUITE -type f -name $EXE_NAME`
    if [ -z "$EXE_PATH" ]; then
        echo "Cannot find \"$EXE_NAME\" in SDK/test_suite."
        rm -rf $temp_folder
        exit 1
    fi
    #echo $EXE_PATH
    install_ha $EXE_PATH
    rm -rf $temp_folder
    exit 0
fi

# unless, install all tests.
echo "start to install all exes ... " `date -R`
count=0
find $TEST_SUITE -type f | while read file; do

    install_ha $file

    let "count+=1"
    #if [ $count -eq 3 ]; then
    #    exit 0
    #fi
done
rm -rf $temp_folder
echo "finish to install $count HAs. " `date -R`
