#!/bin/bash

# Create coverage data and html by lcov tools.
# lcov tool will use gcov/llvm-cov tool to calculate gcda data.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function usage()
{
    printf 'Calculate coverage results with coverage tools and show coverage in HTML.\n'
    printf '%s [arguments] [data folder]\n' "$0"
    printf '  [data folder]  there are coverage data(.gcda) and config(.gcno) files.\n'
    printf '                 gcov created will be placed in this folder.\n'
    printf '                 If not set, data folder will be "out".\n'
    printf 'arguments:\n'
    printf '  -h/--help      show help.\n'
    printf '  -s/--show      tool show html, if not set, use firefox.\n'
    printf '  -g/--gcov      tool to handle gcda, if not set, use "llvm-cov gcov".\n'
    printf '  -r/--source    place where is source code.\n'
}

# Use getopt to parse arguments.
ARGS=`getopt -o hs:g:r: --long help,show:,gcov:,source: -n '' -- "$@"`
# Reorder the arguments.
eval set -- "${ARGS}"

ARG_DATA_PATH=
ARG_SHOW_TOOL="firefox"
ARG_GCOV_TOOL="${SCRIPT_DIR}/nbl_llvm_cov.sh"
ARG_SOURCE_PATH=

while true; do
    case "$1" in
        -s|--show)
            ARG_SHOW_TOOL="$2"
            shift 2
            ;;
        -g|--gcov)
            ARG_GCOV_TOOL="$2"
            shift 2
            ;;
        -r|--source)
            ARG_SOURCE_PATH="$2"
            shift 2
            ;;
        -h|--help)
            usage 1>&2;
            exit 0
            ;;
        *)
            break;
    esac
done

ARG_DATA_PATH="$2"

# check argument

if [ -z "$ARG_DATA_PATH" ]; then
    ARG_DATA_PATH=out
fi

if [ ! -d "$ARG_DATA_PATH" ]; then
    echo "TAR file \"${ARG_DATA_PATH}\" doesn't exist."
    exit 1
fi

OPT_SOURCE=
if [ -n "${ARG_SOURCE_PATH}" ]; then
  OPT_SOURCE="-b ${ARG_SOURCE_PATH}"
fi

# create temp files of lcov by gcda and gcno.
# ignore some error (has gcno, but no gcda, on static library test).
# lcov arguments:
# -o : output calculated result to tracefile(lcov format).
# -d : specify the location where the program was compiled(.gcno) and run data(.gcda). (multiple setting)
# -b : base direcoty for relative directoy, for source code. (only one)
SDK_HOME=${SCRIPT_DIR}/../.. lcov --ignore-errors gcov --ignore-errors graph --ignore-errors source \
  -c --gcov-tool ${ARG_GCOV_TOOL} -o ${ARG_DATA_PATH}/lcov.tmp \
  -d ${ARG_DATA_PATH} \
  ${OPT_SOURCE} \

# create html by genhtml with lcov temp data.
genhtml ${ARG_DATA_PATH}/lcov.tmp --ignore-errors source -o ${ARG_DATA_PATH}/html

# show html.
${ARG_SHOW_TOOL} ${ARG_DATA_PATH}/html/index.html
