# Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

# There are 4 projects showing how to use nbl_fwk API.
# - test_nbl_fwk_ha and test_nbl_fwk_ha_other are two HA.
#   test_nbl_fwk_ca and test_nbl_fwk_ca_android are CA running different OS with same source to
#   communicate with above HAs.
# - license is used to show license API.
# - secmgr is used to show secmgr API.
# - simple_fwk_ca / simple_fwk_ha are a group simple example for nbl_fwk API.

sub= ta1 ta2 ta3

sub_clean=$(patsubst %,%_clean,${sub})

.PHONY:all ${sub} ${sub_clean} run run-android

all: ${sub}

${sub}:
	make -C $@ all

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}
