#!/bin/bash

# Copyright 2021 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e; [[ "${TRACE}" ]] && set -x

function usage() {
  printf '%s: [-h/l]\n' "$0"
  printf '  -i input : input directory.\n'
  printf '  -o staging_file : output staging_file for bazel rules.\n'
  printf '  -h      : show help.\n'
}

while getopts "i:o:h" opt; do
  case "${opt}" in
    i)
      INPUT_DIR=${OPTARG}
      ;;
    o)
      STAGING_FILE=${OPTARG}
      ;;
    h) usage 1>&2 ; exit 0 ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done

ABS_INPUT_DIR="$(realpath ${INPUT_DIR})"
ABS_STAGING_FILE="$(realpath ${STAGING_FILE})"

# get the path of this command, and then get the path of sdk.
# - CMD_DIR should be "<sdk>/run/"
CMD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SDK_HOME=$(dirname $CMD_DIR)
NODEJS=$SDK_HOME/host/nodejs
CSS_VALIDATOR=$SDK_HOME/host/javascript/css_validator

pushd $CSS_VALIDATOR > /dev/null

NO_UPDATE_NOTIFIER=true PATH=$NODEJS/bin:$PATH npm --silent run validate ${ABS_INPUT_DIR}

touch ${ABS_STAGING_FILE}

popd > /dev/null
