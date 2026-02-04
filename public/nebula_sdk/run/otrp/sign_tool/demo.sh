#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
sign_tool_path=$basepath/secmgr_sign_tool.sh
get_config_tool_path=$basepath/secmgr_get_config.sh

mkdir ./out

$sign_tool_path --ha_path $basepath/../../../examples/hee/libc/out/test-libc --sp_private_key_pem_path $basepath/../key_store_test/sp/sign_cert/private_key.pem --spaik_public_key_path $basepath/../key_store_test/spaik/spaik_public_key.pem --out_file $basepath/out/enc_ha.txt

$get_config_tool_path --enc_ha_path $basepath/out/enc_ha.txt --out_file $basepath/out/manifest_config.txt
