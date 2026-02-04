#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/../../../examples/security_domain/tui_sd_default/out/provision/key_store_test
rm -rf ${base_out_path}/*
cd $basepath/../../../examples/security_domain/tui_sd_default

make SD_ALLOW=0

make provision

$basepath/../provision_tool/provision_create_sd_by_config.sh -c $basepath/../key_store_test --sd_privilege_file $basepath/../../../examples/security_domain/tui_sd_default/sd.privilege.allow -o $basepath/../../../examples/security_domain/tui_sd_default/out/provision

cd -

#push data to qemu
    $basepath/../provision_tool/push_qemu.sh $base_out_path /root/ree/secmgr/secmgr_provision_create_sd_config_privilege
#install data
    $basepath/../provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_provision_create_sd_config_privilege
