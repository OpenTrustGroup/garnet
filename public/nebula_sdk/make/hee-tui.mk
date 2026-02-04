# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

NODEJS_HOME:=$(abspath ${SDK_HOME}/host/nodejs/)
CUR_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
CSS_VALIDATOR_DIR:=${SDK_HOME}/host/javascript/css_validator

NPM:=PATH=${NODEJS_HOME}/bin:$(PATH) npm --silent

create-far: validate-css

.PHONY: validate-css
validate-css:
	$(NOECHO)cd ${CSS_VALIDATOR_DIR} && $(NPM) run validate $(CUR_DIR)

include ${SDK_HOME}/make/hee-far.mk
