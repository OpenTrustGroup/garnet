#!/bin/bash

# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

basepath=$(cd `dirname $0`; pwd)
key_store_path=$basepath/../key_store_test

nbl_out_path=${FUCHSIA_DIR}/out

mkdir -p ${FUCHSIA_DIR}/out/secmgr/secmgr_test_ha/temp
base_out_path=${FUCHSIA_DIR}/out/secmgr/secmgr_test_ha
export secmgr_provision_tool_path=${FUCHSIA_DIR}/out/secmgr/build/secmgr_provision_tool.jar
export secmgr_sign_tool_path=${FUCHSIA_DIR}/out/secmgr/build/secmgr_sign_tool.jar
far_tool_path=$basepath/../elf_tool/far

#get_device_state
$basepath/provision_get_device_state.sh --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --tsm_cert_path $key_store_path/tsm/sign_cert/tsm_sign.pem --tsm_ocsp_path $key_store_path/tsm/sign_cert/ocsp/tsm_sign_resp.pem --out_file_path $base_out_path/provision_get_device_state.json

#delete sd
$basepath/provision_delete_sd.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_delete_sd.json --delete_ha "true"

#create sd
$basepath/provision_create_sd.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_create_sd.json

#demo1
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/package/uuid_svr/uuid_svr.far --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo1.json --ha_id 28efb4a7-d3da-410c-9850-6700378ee250 --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha

#demo2
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/uuid_cli --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo2.json --ha_id 905bdb85-2dc5-429d-c952-72354272418d --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha

#demo3
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/uuid_cli.nu --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo3.json --ha_id 905bdb86-2de5-4299-c932-72221174418d --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha

#demo4
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/package/uuid_cli_far/uuid_cli_far.far --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo4.json --ha_id 38ba0cda-df0e-11e4-9869-233fb6ae4795 --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha

#demo5
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/exe.unstripped/ta_big --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo5.json --ha_id 905bdb20-2dc5-429d-c952-72354289418d --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha

#demo6
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/restart_test --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo6.json --ha_id 905bdb21-2dc5-429d-c952-72354889418d --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha

#demo7
#sign ha
$basepath/../sign_tool/secmgr_sign_tool.sh --ha_path $nbl_out_path/arm64/package/restart_test_far/restart_test_far.far --sp_private_key_pem_path $key_store_path/sp/sign_cert/private_key.pem --spaik_public_key_path $key_store_path/spaik/spaik_public_key.pem --out_file $base_out_path/temp/temp_sign_ha --far_tool_file $far_tool_path
#install ha
$basepath/provision_install_ha.sh --tsm_id test.tsm.goldenrivertek.com --sp_id debug_spid --hee_name test_tee_name --sd_name debug_sdname --sp_cert_path $key_store_path/sp/sign_cert/sp_sign.pem --hee_cert_path $key_store_path/hee/sign_cert/hee_sign.pem --tsm_private_key_path $key_store_path/tsm/sign_cert/private_key.pem --out_file_path $base_out_path/provision_install_ha_demo7.json --ha_id 38ba0cda-df0f-11e4-9870-233fb6ae4780 --ha_path $base_out_path/temp/temp_sign_ha
rm $base_out_path/temp/temp_sign_ha
