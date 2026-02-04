#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
debug_tool=$basepath/secmgr_debug_tool.jar

USAGE='usage: Delete all security domain and ha.\n
         --ssh_ip (Optional) SSH IP address when running qemu.The default is 192.168.3.53.\n
         --temp_folder Folder path to save temporary data.\n
         -l Light edition.If the init command(secmgr_init.sh) has been executed.\n
            \tUse "-l" to omit the initialization step. The script will run faster.\n
         -d Run in the device when the "-d" parameter is set, otherwise run in QEMU.\n
         -h Show help.\n
         -v Full log output.\n'

echo "start clean"

#parse common arguments.
USE_MY_OPTS="-o hdvl --long ssh_ip:,temp_folder:"
source $basepath/_parse_common_args.sh

# Get the all params by function.
opts_all_params

eval java -jar ${debug_tool} clean ${ARG_ALL_PARAMS}

echo "clean finish"
