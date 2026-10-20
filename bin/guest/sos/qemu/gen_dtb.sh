#!/bin/bash

set -eo pipefail; [[ "${TRACE}" ]] && set -x

base_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dts_path="${base_path}/tbox_dts"

if [[ -z ${1} ]]; then
    echo "Usage: ${0} <output_dtb_path>"
    exit 1
fi

dts_path=$(realpath ${dts_path})
dtb_path="${1}"

mkdir -p "$(dirname ${dtb_path})"

dtc -o ${dtb_path} -S 4096 ${dts_path}
