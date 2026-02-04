#!/bin/bash

# get COVERAGE tar file, and extract gcda files from TAR file,
# and invoke llvm gcov to convert gcda & gcno to gcov.
# In Qemu, check file list in tar file.
#   cd /var/tmp/nss && tar t -v -f my_coverage.tar

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function usage()
{
    printf 'Clean old data (.gcda), and find tar file in TARET, at last extract coverage files (*.gcda) from TAR file to OUT.\n'
    printf '%s [arguments] <coverage name>\n' "$0"
    printf '  <coverage name>  file name is *.tar including coverage files (*.gcda).\n'
    printf 'arguments:\n'
    printf '  -h/--help        show help.\n'
    printf '  -t/--target <target>  Target can be "device" or "qemu"(default).\n'
    printf '                 And then use corresponding command to qemu UUID or name of TA.\n'
    printf '                 If not set, set "qemu" on default.\n'
    printf '  -o/--out <path>  export coverage files(*.gcda) into this path, such as out.\n'
    printf '                   If not set, set "out" on default.\n'
    printf '  -i/--ta-id <uuid/name>  uuid or name of TA. If set, will wait for end of it.\n'
}

# Use getopt to parse arguments.
ARGS=`getopt -o ht:o:i: --long help,target:,out:,ta-id: -n '' -- "$@"`
# Reorder the arguments.
eval set -- "${ARGS}"

ARG_OUT=
ARG_TAR_NAME=
ARG_TA_ID=
ARG_TARGET="qemu"

while true; do
    case "$1" in
        -o|--out)
            ARG_OUT="$2";
            shift 2
            ;;
        -i|--ta-id)
            ARG_TA_ID="$2";
            shift 2
            ;;
        -t|--target)
            ARG_TARGET="$2";
            shift 2
            ;;
        -h|--help)
            usage 1>&2;
            exit 0;;

        *)
            break;
    esac
done

ARG_TAR_NAME="$2"

# check arguments.

if [ -z "$ARG_TAR_NAME" ]; then
    echo "Please input name of coverage, such as \"coverage.tar\"."
    usage
    exit 1
fi

if [ -z "$ARG_OUT" ]; then
    ARG_OUT=out
fi

if [ ! -d "$ARG_OUT" ]; then
    echo "Folder (${ARG_OUT}) doesn't exist."
    exit 1
fi


if [ "${ARG_TARGET}" != "qemu" -a "${ARG_TARGET}" != "device" ]; then
    echo "Target should be qemu/device."
    usage
    exit 1
fi

# wait for end of TA.
if [ -n "${ARG_TA_ID}" ]; then
    echo "Waiting for end of TA(${ARG_TA_ID})."
    ${SCRIPT_DIR}/nbl_wait_for_ta_end.sh -t ${ARG_TARGET} ${ARG_TA_ID}
fi


# remove old TAR.
if [ -f ${ARG_OUT}/${ARG_TAR_NAME} ]; then
    rm ${ARG_OUT}/${ARG_TAR_NAME}
fi

# Get TAR from target.
if [ "${ARG_TARGET}" == "qemu" ]; then
    scp root@192.168.3.53:/var/tmp/nss/${ARG_TAR_NAME} ${ARG_OUT}
    ssh root@192.168.3.53 "rm /var/tmp/nss/${ARG_TAR_NAME}"
else
    echo "Not impl: get tar from device."
    exit 1
fi

# clean old *.gcda and *.gcov files in OUT folder.
$(cd $ARG_OUT && find -name "*.gcda" -delete && find -name "*.gcov" -delete)

# extract *.gcda from TAR file to out folder.
tar xvf ${ARG_OUT}/${ARG_TAR_NAME} -C ${ARG_OUT} > /dev/null
