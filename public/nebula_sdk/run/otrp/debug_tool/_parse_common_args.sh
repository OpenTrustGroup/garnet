#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# provide functions for parsing common command arguments.

usage_str="usage"
no_verbose_str=" >/dev/null"

VERBOSE=

SD_NAME_PARAM=
HA_ID_PARAM=
HA_PATH_PARAM=
DEVICE_PARAM=
VERBOSE_PARAM=
LIGHT_PARAM=
ENFORCE_PARAM=
TEMP_FOLDER_PARAM=

# parse arguments.
if [ -z "$USE_MY_OPTS" ]; then
	USE_MY_OPTS="-o hdvlf --long sd_name:,ha_id:,ha_path:,run_ha_param:,ssh_ip:,temp_folder:,sd_privilege_file:"
fi
PROGRAM_NAME=`basename $0`

ARGS=`getopt $USE_MY_OPTS -n "$PROGRAM_NAME" -- "$@"`
if [ $? != 0 ]; then
	echo "Failed to parse arguments. $?"
	exit 1
fi

# reorder arguments.
eval set -- "${ARGS}"

while true; do
	case "$1" in
		--run_ha_param)
			# Because params can contain SPACE, so should set format is "\"1 2 3\""
			RUN_ARG=" --run_ha_param \"\\\"$2\\\"\""
			RUN_HA_ARG=$2
			shift 2;;
		-d)
			DEVICE=1
			shift;;
		-v)
			VERBOSE=1
			shift;;
		-l)
			LIGHT=1
			shift;;
		-f)
			ENFORCE=1
			shift;;
		--sd_name)
			SD_NAME=$2
			shift 2;;
		--ha_id)
			HA_ID=$2
			shift 2;;
		--ha_path)
			HA_PATH=$2
			shift 2;;
                --ssh_ip)
                        SSH_IP=$2
                        shift 2;;
                --temp_folder)
                        TEMP_FOLDER=$2
                        shift 2;;
        --sd_privilege_file)
			SD_PRIVILEGE_FILE=$2
                        shift 2;;
		--)
			shift
			break;;
		-h)
			echo -e ${USAGE}
			exit 0;;
		*)
			echo -e ${USAGE}
			exit 1;;
	esac
done

if [ -n "$SD_NAME" ]; then
	SD_NAME_PARAM="--sd_name $SD_NAME"
fi

if [ -n "$HA_ID" ]; then
	HA_ID_PARAM="--ha_id $HA_ID"
fi

if [ -n "$HA_PATH" ]; then
	HA_PATH_PARAM="--ha_path $HA_PATH"
fi

if [ -n "$DEVICE" ]; then
	DEVICE_PARAM="--d"
fi

if [ -z "$VERBOSE" ]; then
	# If don't need VERBOSE, export LOG to NULL.
	VERBOSE_PARAM=">/dev/null"
fi

if [ -n "$LIGHT" ]; then
	LIGHT_PARAM="--l"
fi

if [ -n "$ENFORCE" ]; then
	ENFORCE_PARAM="--f"
fi

if [ -n "$SSH_IP" ]; then
	SSH_IP_PARAM="--ssh_ip $SSH_IP"
fi

if [ -n "$TEMP_FOLDER" ]; then
	TEMP_FOLDER_PARAM="--temp_folder $TEMP_FOLDER"
fi

if [ -n "$SD_PRIVILEGE_FILE" ]; then
	SD_PRIVILEGE_FILE_PARAM="--privilege_file $SD_PRIVILEGE_FILE"
fi

function opts_all_params()
{
	# $VERBOSE_PARAM must be last.
	ARG_ALL_PARAMS="$SD_NAME_PARAM $HA_ID_PARAM $HA_PATH_PARAM $DEVICE_PARAM $LIGHT_PARAM $ENFORCE_PARAM $SSH_IP_PARAM $TEMP_FOLDER_PARAM $SD_PRIVILEGE_FILE_PARAM $RUN_ARG $VERBOSE_PARAM"
}
