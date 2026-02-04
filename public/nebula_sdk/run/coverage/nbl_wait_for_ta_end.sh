#!/bin/bash

# wait for TA ends.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function usage()
{
    printf 'Wait for TA ends with querying TA Process by UUID.\n'
    printf '%s [arguments] <uuid/name>\n' "$0"
    printf '  <uuid/name>  UUID of TA or Name which can show in ps command.\n'
    printf 'arguments:\n'
    printf '  -h/--help    Show help.\n'
    printf '  -t/--target <target>  Target can be "device" or "qemu"(default).\n'
    printf '                 And then use corresponding command to qemu UUID or name of TA.\n'
    printf '  -m/--max <seconds> Wait for max seconds, default is 60s\n'
}

# Use getopt to parse arguments.
ARGS=`getopt -o ht:m: --long help,target:max: -n '' -- "$@"`
# Reorder the arguments.
eval set -- "${ARGS}"

ARG_TARGET="qemu"
ARG_UUID_NAME=
ARG_MAX_SEC=60

while true
do
    case "$1" in
        -t|--target)
            ARG_TARGET="$2";
            shift 2
            ;;
        -m|--max)
            ARG_MAX_SEC="$2";
            shift 2
            ;;
        -h|--help)
            usage 1>&2;
            exit 0;;

        *)
            break;
    esac
done

ARG_UUID_NAME="$2"

if [ -z "$ARG_UUID_NAME" ]; then
    echo "Please set UUID or Name of TA."
    usage
    exit 1
fi

# Length of process name in ps command is 31 char at max.
ARG_UUID_NAME=${ARG_UUID_NAME:0:31}

if [ "${ARG_TARGET}" != "qemu" -a "${ARG_TARGET}" != "device" ]; then
    echo "Target should be qemu/device."
    usage
    exit 1
fi

if [ "${ARG_TARGET}" == "qemu" ]; then
    for (( i=0; i<${ARG_MAX_SEC}; i++ )); do
        sleep 1
        ssh root@192.168.3.53 "nblruncmd ps" | grep ${ARG_UUID_NAME}
        if [ $? -ne 0 ]; then
            break;
        fi
    done

elif [ "${ARG_TARGET}" == "device" ]; then
    # Use adb shell command
    echo ""
fi

exit 0
