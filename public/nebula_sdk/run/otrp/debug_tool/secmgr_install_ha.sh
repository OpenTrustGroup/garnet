#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
debug_tool=$basepath/secmgr_debug_tool.jar

USAGE='usage: Install ha to the security domain.\n
              --sd_name The name of the security domain that ha will be installed in.\n
              --ha_id (Optional)HA identifier.If this parameter is not set, it will be parsed from the image of HA.\n
              --ha_path HA image path.\n
              --ssh_ip (Optional) SSH IP address when running qemu.The default is 192.168.3.53.\n
              --temp_folder (Optional)Folder path to save temporary data.\n
              -d Run in the device when the "-d" parameter is set, otherwise run in QEMU.\n
              -h Show help.\n
              -v Full log output.\n
              -l Light edition.If the init command(secmgr_init.sh) has been executed.\n
                 \tUse "-l" to omit the initialization step. The script will run faster.\n
              -f Install HA if HA does not exist or update HA.\n'

echo "start install ha ..."

#parse common arguments.
source $basepath/_parse_common_args.sh

far_file_path=$basepath/../elf_tool/far

if [ -z "$HA_PATH_PARAM" ]; then
    echo "ERROR:ha path is empty"
    echo -e ${USAGE}
    exit 0
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

eval java -jar ${debug_tool} install_ha --far_path ${far_file_path} ${ARG_ALL_PARAMS}

echo "install ha finish"
