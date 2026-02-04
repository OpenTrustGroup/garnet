#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Used in test-sdk qemu
# Delete the SD where "hello world" HA is located. Reinstall HA through the configuration file
set -e

basepath=$(cd `dirname $0`; pwd)

base_out_path=$basepath/../../../examples/security_domain/tui_sd_default/out/provision/key_store_test
rm -rf base_out_path/*
cd $basepath/../../../examples/security_domain/tui_sd_default

make

make provision

cd -

#push data to qemu
    $basepath/../provision_tool/push_qemu.sh $base_out_path /root/ree/secmgr/secmgr_provision_update_sd_privilege
#install data
    $basepath/../provision_tool/provision_install_data.sh --data_path /root/ree/secmgr/secmgr_provision_update_sd_privilege
