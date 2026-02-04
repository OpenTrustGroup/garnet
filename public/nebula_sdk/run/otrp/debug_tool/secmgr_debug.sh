#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#set -x && set -e

basepath=$(cd `dirname $0`; pwd)
debug_tool=$basepath/secmgr_debug_tool.jar

USAGE='usage: It is used to quickly install ha into the device/qemu for debug.Create a security domain and ha will be installed in it.\n
              \tHA will overwrite if it already exists. It will be started after the installation is complete.\n
          --ha_path Ha image path.\n
          --sd_name (optional)The name of the security domain.Use the default security domain if this parameter is empty.\n
          --ha_id (optional)Ha identifier.If it is empty, it will be parsed from the HA manifest configuration.\n
          --run_ha_param (Optional) Parameters used to run HA
          --ssh_ip (Optional) SSH IP address when running qemu.The default is 192.168.3.53.\n
          --temp_folder Folder path to save temporary data.\n
          -d Run in the device when the "-d" parameter is set, otherwise run in QEMU.\n
          -a arg : Run HA with arg.\n
          -f : force to reinstall HA if exists.\n
          --sd_privilege_file (optional) set privilege file of sd.\n
          -h Show help.\n
          -v Full log output.\n'

echo "start debug ..."

#parse common arguments.
source $basepath/_parse_common_args.sh

far_file_path=$basepath/../elf_tool/far

if [ -z "$HA_PATH_PARAM" ]; then
    echo "ERROR:ha path is empty"
    echo -e ${USAGE}
    exit 1
fi

if [ -z "$SD_NAME_PARAM" ]; then
    # If not set sd name, use default sd name.
    SD_NAME_PARAM=" --sd_name debug_sd"
fi

if [ -z "$TEMP_FOLDER_PARAM" ]; then
    TEMP_FOLDER_PARAM="--temp_folder $basepath/debug"
fi

# Get the all params by function.
opts_all_params

# Execute java program.
# 1. Because run_ha_param can contains params with SPACE,
# - so the arguments of java should be --run_ha_param "\"$RUN_HA_ARG\""
# - for exampel : "\"1 2 3\""
# 2. And then use "eval" to parse the command again.

eval java -jar ${debug_tool} debug --far_path ${far_file_path} ${ARG_ALL_PARAMS}

echo "debug finish"
