// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <nbl_fwk_ree/service.h>

#include "vtrust_cp_ree.h"
#include "data/include/script_py.h"
#include "data/include/script_sign.h"
#include "data/include/sign_publickey_data.h"

#define LOG_TAG "Simple Vtrust CP Client"

int main(int argc, char *argv[]) {
	NBL_LOGI("Run vtrust cp demo\n");
	int rc = 0;
	nbl_cp_req_t req;
	nbl_cp_res_t res;
	res.data = NULL;

	req.mp_script = malloc(script_py_len + 1);
	if (req.mp_script == 0) {
		NBL_LOGE(
				"Run vtrust cp demo failed (%d). malloc script memory failed\n",
				rc);
		goto failed;
	}
	memset(req.mp_script, 0, script_py_len + 1);
	memcpy(req.mp_script, script_py, script_py_len);

	req.mp_param = "Hello World";
	req.mp_param_size = strlen(req.mp_param);

	req.sign_publickey = malloc(sign_publickey_data_len + 1);
	if (req.sign_publickey == 0) {
		NBL_LOGE(
				"Run vtrust cp demo failed (%d). malloc sign public key memory failed\n",
				rc);
		goto failed;
	}
	memset(req.sign_publickey, 0, sign_publickey_data_len + 1);
	memcpy(req.sign_publickey, sign_publickey_data, sign_publickey_data_len);
	req.sign_publickey_size = sign_publickey_data_len;

	req.mp_script_sign = malloc(script_sign_len + 1);
	if (req.mp_script_sign == 0) {
		NBL_LOGE(
				"Run vtrust cp demo failed (%d). malloc script sign memory failed\n",
				rc);
		goto failed;
	}
	memset(req.mp_script_sign, 0, script_sign_len + 1);
	memcpy(req.mp_script_sign, script_sign, script_sign_len);
	req.mp_script_sign_size = script_sign_len;

	rc = nbl_run_ta(&req, &res);

	if (rc != NBL_CP_REE_SUCCESS) {
		NBL_LOGE("Run vtrust cp demo failed (%d).\n", rc);
	} else {
		NBL_LOGI("Return status:%d\n", res.status);
		NBL_LOGI("Return data_size:%ld\n", res.data_size);
		NBL_LOGI("Return data:%s\n", res.data);
		NBL_LOGI("Run vtrust cp OK!\n");
	}

	failed:

	if (res.data != NULL) {
		free(res.data);
		res.data = NULL;
	}
	if (req.mp_script != NULL) {
		free(req.mp_script);
		req.mp_script = NULL;
	}
	if (req.sign_publickey != NULL) {
		free(req.sign_publickey);
		req.sign_publickey = NULL;
	}
	if (req.mp_script_sign != NULL) {
		free(req.mp_script_sign);
		req.mp_script_sign = NULL;
	}

	return rc;
}
