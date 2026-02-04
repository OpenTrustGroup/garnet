#!/bin/bash

# Invoke cov tools(such as llvm-cov or gcov) to calcuate gcda & gcno to coverage result.
# And create gcov which can show invoking count of source code.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function usage()
{
    printf 'Calculate coverage results with coverage tools and create gcov file which show coverage in source.\n'
    printf '%s [arguments] [data folder]\n' "$0"
    printf '  [data folder]  there are coverage data(.gcda) and config(.gcno) files.\n'
    printf '                 gcov created will be placed in this folder.\n'
    printf '                 If not set, data folder will be "out".\n'
    printf 'arguments:\n'
    printf '  -h/--help        show help.\n'
}

# Use getopt to parse arguments.
ARGS=`getopt -o h --long help -n '' -- "$@"`
# Reorder the arguments.
eval set -- "${ARGS}"

ARG_DATA_PATH=

while true; do
    case "$1" in
        -h|--help)
            usage 1>&2;
            exit 0;;

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

# 1. use gcov/llvm-cov gcov to create coverage summary result and source coverage.
COV=${SCRIPT_DIR}/../../toolchains/clang/bin/llvm-cov

for f in `cd ${ARG_DATA_PATH} && find -name "*.gcda"`; do
    GCDA_PATH="$f"
    GCNO_PATH=${GCDA_PATH%.gcda}.gcno
    # xxx.gcov file created by gcov will be placed in current dir and without dir, and just a name.
    GCOV_PATH=${GCDA_PATH%.gcda}.gcov
    GCOV_NAME=`basename ${GCOV_PATH}`

    # calculate coverage and create gcov files in current dir.
    ${COV} gcov ${ARG_DATA_PATH}/${GCNO_PATH}

    # move xxx.gcov into out folder.
    if [ -f "./${GCOV_NAME}" ]; then
        mv ./${GCOV_NAME} ${ARG_DATA_PATH}/${GCOV_PATH}
    else
        echo -e "./${GCOV_NAME} is not created.\n"
    fi

done

# If run with c++ source, .gcov of STL class will be created.
if [ -f "*.gcov" ]; then
    mv *.gcov ${ARG_DATA_PATH}
fi
