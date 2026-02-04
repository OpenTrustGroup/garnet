# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# There are many group of projects to show how to use nbl_fwk API.
# - test_nbl_fwk_ta and test_nbl_fwk_ta_other are two services.
#   test_nbl_fwk_ca and test_nbl_fwk_ca_android are client running different OS (Android and Linux) with same source.
#   test_nbl_fwk_ca and test_nbl_fwk_ca_android start two services about test_nbl_fwk_ta and test_nbl_fwk_ta_other,
#   and then client will send data to test_nbl_fwk_ta, at last test_nbl_fwk_ta will send data to test_nbl_fwk_ta_other.
# - simple_fwk_ca_client/simple_fwk_ta_client/simple_fwk_service are a group simple examples for commnication between service and client.
# - license/nmt_ree/nmt_tee are used to show license and Nebula Manufacture Tool(NMT) API.
# - install/install_provision is used to show installing TA.
# - time_ta is used to show secure time API.
# - libsec_conn_ree and libsec_conn_tee are used to show secure pipe.

sub= simple_fwk_ca_client simple_fwk_ta_client simple_fwk_service \
	test_nbl_fwk_ta test_nbl_fwk_ta_other test_nbl_fwk_ca test_nbl_fwk_ca_android \
	install install_provision \
	libsec_conn_ree libsec_conn_tee \
	license nmt_ree nmt_tee \
	time_ta

sub_clean=$(patsubst %,%_clean,${sub})

.PHONY:all ${sub} ${sub_clean} run run-android

# It's should be the first target, and then it's the default target.
all: ${sub}

${sub}:
	make -C $@ all

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

# Run in QEMU
run: run-qemu

run-qemu:
	make -C test_nbl_fwk_ha install-qemu
	make -C test_nbl_fwk_ha_other install-qemu
	make -C test_nbl_fwk_ca run
	make -C secmgr run-qemu
	make -C secmgr_provision run-qemu

# Run in Android(Real device)
# TODO: It's NOT tested.
run-device:
	make -C test_nbl_fwk_ha install-device
	make -C test_nbl_fwk_ha_other install-device
	make -C test_nbl_fwk_ca_android run
