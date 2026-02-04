#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
debug_tool=$basepath/secmgr_debug_tool.jar

USAGE='usage: Run ha by ha id.\n
              --ha_id Ha id.Unique identifier for ha installation.\n
              --run_ha_param (Optional) Parameters required to run HA.\n
              --ssh_ip (Optional) SSH IP address when running qemu.The default is 192.168.3.53.\n
              -d Run in the device when the "-d" parameter is set, otherwise run in QEMU.\n
              -h Show help.\n
              -a arg : Run HA with argument.\n
              -v Full log output.\n'

echo "start run ha"

#parse common arguments.
source $basepath/_parse_common_args.sh

if [ -z "$HA_ID" ]; then
    echo "ERROR:ha id is empty"
    echo -e ${USAGE}
    exit 0
fi

# Get the all params by function.
opts_all_params

eval java -jar ${debug_tool} run_ha ${ARG_ALL_PARAMS}

echo "run ha finish"
