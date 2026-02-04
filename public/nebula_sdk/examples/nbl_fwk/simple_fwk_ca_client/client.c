// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nbl_fwk_ree/service.h>

#define LOG_TAG "Simple REE Client"

#define SRV_A_NAME "com.nebula.simple_hee.a"
#define SRV_B_NAME "com.nebula.simple.hee.b"

#define BUF_MSG "Hello World use tipc!"
#define SHM_MSG "Hello World use shm!"

enum {
	CMD_SEND_INT = 1,
	CMD_SEND_DATA = 2,
	CMD_MAP_SHM = 3,
	CMD_SEND_SHM_CONTITUE = 4,
	CMD_UNMAP_SHM = 5,
	CMD_SHOW_SESSION = 6
};

static int _run_service_a_demo() {
	int rc = NBL_NO_ERROR;
	nbl_chn_t *chn;
	char *srv_name = SRV_A_NAME;
	int cmd;
	nbl_stm_t *request;
	nbl_stm_t *response;
	nbl_shm_t *shm = NULL;
	void *shm_base;
	int ack;

	rc = nbl_chn_open(srv_name, 0, &chn);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Connect to %s failed.\n", srv_name);
		goto connect_failed;
	}

	request = nbl_stm_create();
	response = nbl_stm_create();

	cmd = CMD_SEND_INT;
	nbl_stm_add_int(request, cmd);
	nbl_stm_add_int(request, 0x12345678);
	rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Invoke command (%d) failed.\n", cmd);
		goto send_int_failed;
	} else {
		nbl_stm_next_int(response, &ack);
		NBL_LOGI("Invoke command get ack (%d).\n", ack);
	}

	cmd = CMD_SEND_DATA;
	nbl_stm_add_int(request, cmd);
	nbl_stm_add_buf(request, strlen(BUF_MSG) + 1, BUF_MSG);
	rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Invoke command (%d) failed.\n", cmd);
		goto send_buf_failed;
	} else {
		nbl_stm_next_int(response, &ack);
		NBL_LOGI("Invoke command get ack (%d).\n", ack);
	}

	rc = nbl_shm_alloc(chn, strlen(BUF_MSG), NBL_SHM_PERM_RW, &shm, &shm_base);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Alloc shared memory failed (%d).\n", rc);
		goto alloc_shm_failed;
	}

	cmd = CMD_MAP_SHM;
	nbl_stm_add_int(request, cmd);
	nbl_stm_add_shm(request, shm);
	rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Invoke command (%d) failed.\n", cmd);
		goto send_shm_failed;
	} else {
		nbl_stm_next_int(response, &ack);
		NBL_LOGI("Invoke command get ack (%d).\n", ack);
	}

	cmd = CMD_SEND_SHM_CONTITUE;
	nbl_stm_add_int(request, cmd);
	strcpy((char *)shm_base, SHM_MSG);
	rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Invoke command (%d) failed.\n", cmd);
		goto send_shm_failed;
	} else {
		nbl_stm_next_int(response, &ack);
		NBL_LOGI("Invoke command get ack (%d).\n", ack);
	}

	cmd = CMD_SHOW_SESSION;
	nbl_stm_add_int(request, cmd);
	rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Invoke command (%d) failed.\n", cmd);
		goto send_shm_failed;
	} else {
		nbl_stm_next_int(response, &ack);
		NBL_LOGI("Invoke command get ack (%d).\n", ack);
	}

	cmd = CMD_UNMAP_SHM;
	nbl_stm_add_int(request, cmd);
	rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Invoke command (%d) failed.\n", cmd);
		goto send_shm_failed;
	} else {
		nbl_stm_next_int(response, &ack);
		NBL_LOGI("Invoke command get ack (%d).\n", ack);
	}

	nbl_shm_free(shm);
	nbl_stm_destroy(request);
	nbl_stm_destroy(response);
	nbl_chn_close(chn);

	return NBL_NO_ERROR;

send_shm_failed:
	nbl_shm_free(shm);
alloc_shm_failed:
send_buf_failed:
send_int_failed:
	nbl_stm_destroy(request);
	nbl_stm_destroy(response);
	nbl_chn_close(chn);
connect_failed:
	return rc;
}

static int _run_service_b_demo() {
	int rc = NBL_NO_ERROR;
	nbl_chn_t *chn;
	char *srv_name = SRV_B_NAME;
	char buf[256];

	rc = nbl_chn_open(srv_name, 0, &chn);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Connect to %s failed.\n", srv_name);
		goto connect_failed;
	}

	rc = nbl_chn_write_buf(chn, BUF_MSG, strlen(BUF_MSG) + 1);
	if (rc != strlen(BUF_MSG) + 1) {
		NBL_LOGE("Service B Client write buffer failed (%d).\n", rc);
		goto write_buf_failed;
	}

	rc = nbl_chn_read_buf(chn, buf, sizeof(buf), NBL_TIMEOUT_INFINITY);
	if (rc != strlen(BUF_MSG) + 1) {
		NBL_LOGE("Service B Client read buffer failed (%d).\n", rc);
		goto read_buf_failed;
	} else {
		NBL_LOGI("Service B read buffer (%s).\n", buf);
	}

	nbl_chn_close(chn);

	return NBL_NO_ERROR;

read_buf_failed:
write_buf_failed:
	nbl_chn_close(chn);
connect_failed:
	return rc;
}

int main(int argc, char **argv) {
	int rc = NBL_NO_ERROR;

	rc = _run_service_a_demo();
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Run service A demo failed (%d).\n", rc);
		exit(rc);
	} else {
		NBL_LOGI("Run demo A OK!\n");
	}

	rc = _run_service_b_demo();
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Run service B demo failed (%d).\n", rc);
		exit(rc);
	} else {
		NBL_LOGI("Run demo B OK!\n");
	}

	return rc;
}
