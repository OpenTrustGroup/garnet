#!/bin/bash

# Copyright 2019 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Dump trace file

CMD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SDK_HOME=$(dirname $CMD_DIR)
TRACEUTIL=${SDK_HOME}/host/x64/traceutil

SSHD_IP=root@192.168.3.53
if [ -n "$SSH_IP" ]; then
    SSHD_IP=root@$SSH_IP
fi

function die(){
  echo "$1"
  exit 1
}

ssh -o StrictHostKeyChecking=no ${SSHD_IP} "nblruncmd ls /data/trace.json" > /dev/null || die "Trace file not found"
ssh -o StrictHostKeyChecking=no ${SSHD_IP} "nblruncmd cat /data/trace.json" > ./trace.json

# TODO: enable trace converter
#${TRACEUTIL} convert trace.json
