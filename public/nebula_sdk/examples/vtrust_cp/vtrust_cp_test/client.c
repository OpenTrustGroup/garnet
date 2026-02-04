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

int test_normal() {
	NBL_LOGI("Test vtrust cp normal start\n");
	int rc = -1;
	int tr = 0;
	nbl_cp_req_t req;
	nbl_cp_res_t res;
	res.data = NULL;

	req.mp_script = malloc(script_py_len + 1);
	if (req.mp_script == 0) {
		NBL_LOGE("Run vtrust cp test failed. malloc script memory failed\n");
		goto failed;
	}
	memset(req.mp_script, 0, script_py_len + 1);
	memcpy(req.mp_script, script_py, script_py_len);

	req.mp_param = "Hello World";
	req.mp_param_size = strlen(req.mp_param);

	req.sign_publickey = malloc(sign_publickey_data_len + 1);
	if (req.sign_publickey == 0) {
		NBL_LOGE(
				"Run vtrust cp test failed. malloc sign public key memory failed\n");
		goto failed;
	}
	memset(req.sign_publickey, 0, sign_publickey_data_len + 1);
	memcpy(req.sign_publickey, sign_publickey_data, sign_publickey_data_len);
	req.sign_publickey_size = sign_publickey_data_len;

	req.mp_script_sign = malloc(script_sign_len + 1);
	if (req.mp_script_sign == 0) {
		NBL_LOGE(
				"Run vtrust cp test failed. malloc script sign memory failed\n");
		goto failed;
	}
	memset(req.mp_script_sign, 0, script_sign_len + 1);
	memcpy(req.mp_script_sign, script_sign, script_sign_len);
	req.mp_script_sign_size = script_sign_len;

	tr = nbl_run_ta(&req, &res);

	if (tr != NBL_CP_REE_SUCCESS) {
		NBL_LOGE("Run vtrust cp test failed (%d).\n", tr);
		rc = -1;
	} else {
		NBL_LOGI("Return status:%d\n", res.status);
		NBL_LOGI("Return data_size:%ld\n", res.data_size);
		NBL_LOGI("Return data:%s\n", res.data);
		NBL_LOGI("Run vtrust cp OK!\n");
	}

	if (res.data == NULL || strcmp("test response data", res.data)) {
		NBL_LOGE("Test vtrust cp failed. responst data not match\n");
		rc = -1;
	} else {
		NBL_LOGI("Test vtrust cp normal success\n");
		rc = 0;
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

int test_sign() {
	NBL_LOGI("Test vtrust cp sign not match start\n");
	int rc = -1;
	int tr = 0;
	nbl_cp_req_t req;
	nbl_cp_res_t res;
	res.data = NULL;

	req.mp_script = malloc(script_py_len + 1);
	if (req.mp_script == 0) {
		NBL_LOGE("Run vtrust cp test failed. malloc script memory failed\n");
		goto failed;
	}
	memset(req.mp_script, 0, script_py_len + 1);
	memcpy(req.mp_script, script_py, script_py_len);

	req.mp_param = "Hello World";
	req.mp_param_size = strlen(req.mp_param);

	req.sign_publickey = malloc(sign_publickey_data_len + 1);
	if (req.sign_publickey == 0) {
		NBL_LOGE(
				"Run vtrust cp test failed. malloc sign public key memory failed\n");
		goto failed;
	}
	memset(req.sign_publickey, 0, sign_publickey_data_len + 1);
	memcpy(req.sign_publickey, sign_publickey_data, sign_publickey_data_len);
	req.sign_publickey_size = sign_publickey_data_len;

	req.mp_script_sign = malloc(10 + 1);
	if (req.mp_script_sign == 0) {
		NBL_LOGE(
				"Run vtrust cp test failed. malloc script sign memory failed\n");
		goto failed;
	}
	memset(req.mp_script_sign, 0, 10 + 1);
	memcpy(req.mp_script_sign, "1234567890", 10);
	req.mp_script_sign_size = 10;

	tr = nbl_run_ta(&req, &res);

	if (tr != NBL_CP_REE_SUCCESS) {
		NBL_LOGE("Run vtrust cp test failed (%d).\n", tr);
	} else {
		NBL_LOGI("Return status:%d\n", res.status);
		NBL_LOGI("Return data_size:%ld\n", res.data_size);
		NBL_LOGI("Return data:%s\n", res.data);
		NBL_LOGI("Run vtrust cp OK!\n");
	}

	if (res.status != NBL_CP_TEE_VERIFY_DATA_SIGNATURE_FAILED) {
		NBL_LOGE("Test vtrust cp sign test failed\n");
		rc = -1;
	} else {
		NBL_LOGI("Test vtrust cp sign not match success\n");
		rc = 0;
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

int test_no_script() {
	NBL_LOGI("Test vtrust cp no script start\n");
	int tr = 0;
	int rc = -1;
	nbl_cp_req_t req;
	nbl_cp_res_t res;
	res.data = NULL;

	req.mp_script = NULL;

	req.mp_param = "Hello World";
	req.mp_param_size = strlen(req.mp_param);

	req.sign_publickey = malloc(sign_publickey_data_len + 1);
	if (req.sign_publickey == 0) {
		NBL_LOGE(
				"Run vtrust cp test failed. malloc sign public key memory failed\n");
		goto failed;
	}
	memset(req.sign_publickey, 0, sign_publickey_data_len + 1);
	memcpy(req.sign_publickey, sign_publickey_data, sign_publickey_data_len);
	req.sign_publickey_size = sign_publickey_data_len;

	req.mp_script_sign = malloc(script_sign_len + 1);
	if (req.mp_script_sign == 0) {
		NBL_LOGE(
				"Run vtrust cp test failed. malloc script sign memory failed\n");
		goto failed;
	}
	memset(req.mp_script_sign, 0, script_sign_len + 1);
	memcpy(req.mp_script_sign, script_sign, script_sign_len);
	req.mp_script_sign_size = script_sign_len;

	tr = nbl_run_ta(&req, &res);

	if (tr != NBL_CP_REE_PARAM_NULL) {
		NBL_LOGE("Test vtrust cp  no script failed %d\n", tr);
		rc = -1;
	} else {
		NBL_LOGI("Test vtrust cp  no script success\n");
		rc = 0;
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

int test_no_script_param() {
	NBL_LOGI("Test vtrust cp script no param start\n");
	int tr = 0;
	int rc = -1;
	nbl_cp_req_t req;
	nbl_cp_res_t res;
	res.data = NULL;

	req.mp_script = malloc(script_py_len + 1);
	if (req.mp_script == 0) {
		NBL_LOGE("Run vtrust cp demo failed. malloc script memory failed\n");
		goto failed;
	}
	memset(req.mp_script, 0, script_py_len + 1);
	memcpy(req.mp_script, script_py, script_py_len);

	req.mp_param = NULL;
	req.mp_param_size = 0;

	req.sign_publickey = malloc(sign_publickey_data_len + 1);
	if (req.sign_publickey == 0) {
		NBL_LOGE(
				"Run vtrust cp demo failed. malloc sign public key memory failed\n");
		goto failed;
	}
	memset(req.sign_publickey, 0, sign_publickey_data_len + 1);
	memcpy(req.sign_publickey, sign_publickey_data, sign_publickey_data_len);
	req.sign_publickey_size = sign_publickey_data_len;

	req.mp_script_sign = malloc(script_sign_len + 1);
	if (req.mp_script_sign == 0) {
		NBL_LOGE(
				"Run vtrust cp demo failed. malloc script sign memory failed\n");
		goto failed;
	}
	memset(req.mp_script_sign, 0, script_sign_len + 1);
	memcpy(req.mp_script_sign, script_sign, script_sign_len);
	req.mp_script_sign_size = script_sign_len;

	tr = nbl_run_ta(&req, &res);

	if (tr != NBL_CP_REE_SUCCESS) {
		NBL_LOGE("Run vtrust cp test script no param failed (%d).\n", tr);
		rc = -1;
	} else {
		NBL_LOGI("Run vtrust cp test script no param success (%d).\n", tr);
                rc = 0;
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

int main(int argc, char *argv[]) {
	NBL_LOGI("Run vtrust cross platform test\n");
	int test_success = 1;
	int rc = 0;
	rc = test_normal();
	if (rc != 0) {
		test_success = 0;
	}
	rc = test_sign();
	if (rc != 0) {
		test_success = 0;
	}
	rc = test_no_script();
	if (rc != 0) {
		test_success = 0;
	}
	rc = test_no_script_param();
	if (rc != 0) {
		test_success = 0;
	}
	if (test_success) {
		NBL_LOGI("Vtrust cross platform All tests PASSED\n");
	} else {
		NBL_LOGI("Vtrust cross platform Some tests FAILED\n");
	}
	return 0;
}
