#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### get the HA ID by a string with any length.
### - make the md5 of "str" as haid.

function usage()
{
    printf 'usage: <str>     calculate a HA id by string.\n'
    printf '  str   a string with any length.\n'
}

if [ $# != 1 ]; then
    usage
    exit 1
fi

ID=`echo -n $1 | openssl md5 | awk -F "=" '{print $2}' | sed "s/ //g"`

HAID="${ID:0:8}-${ID:8:4}-${ID:12:4}-${ID:16:4}-${ID:20:12}"

echo $HAID
