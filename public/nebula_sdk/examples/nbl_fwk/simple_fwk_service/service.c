// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <nbl_fwk/service.h>

#define LOG_TAG "Simple HEE Service"

#define SRV_A_NAME "com.nebula.simple_hee.a"
#define SRV_B_NAME "com.nebula.simple.hee.b"

enum {
	CMD_SEND_INT = 1,
	CMD_SEND_DATA = 2,
	CMD_MAP_SHM = 3,
	CMD_SEND_SHM_CONTITUE = 4,
	CMD_UNMAP_SHM = 5,
	CMD_SHOW_SESSION = 6
};

typedef struct session_a {
	int value;
	int data_size;
	void *buf;
	void *shm_base;
	nbl_shm_t *shm;
	nbl_stm_t *ack;
} session_a_t;

static int _srv_a_connect_handler(nbl_srv_t *srv, nbl_uuid_t peer_uuid) {
	NBL_LOGI("Simple HEE sample service A connect handler invoked.\n");
	return NBL_NO_ERROR;
}

static void _srv_a_open_handler(nbl_chn_t *chn) {
	session_a_t *session = calloc(1, sizeof(session_a_t));
	session->buf = malloc(256);
	session->ack = nbl_stm_create();
	nbl_chn_set_priv(chn, session);
	NBL_LOGI("Simple HEE sample service A open handler invoked.\n");
}

static int _srv_a_stm_handler(nbl_chn_t *chn, nbl_stm_t *stm) {
	int rc;
	int cmd;
	session_a_t *session = nbl_chn_get_priv(chn);

	rc = nbl_stm_next_int(stm, &cmd);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Service A Get cmd failed (%d).\n", rc);
		goto get_cmd_failed;
	}

	switch(cmd) {
	case CMD_SEND_INT:
		nbl_stm_next_int(stm, &session->value);
		break;
	case CMD_SEND_DATA:
		nbl_stm_next_buf(stm, &session->data_size, session->buf);
		break;
	case CMD_MAP_SHM:
		nbl_stm_next_shm(stm, &session->shm);
		nbl_shm_map(session->shm, NBL_SHM_PERM_RW, &session->shm_base);
		break;
	case CMD_SEND_SHM_CONTITUE:
		NBL_LOGI("Service A CMD_SEND_SHM_CONTITUE get message (%s) with shared memory.\n",
				session->shm_base);
		break;
	case CMD_SHOW_SESSION:
		NBL_LOGI("Service A show session (%x, %d, %s, %s).\n", session->value,
				session->data_size, (char *)session->shm_base, (char *)session->buf);
		break;
	case CMD_UNMAP_SHM:
		nbl_shm_unmap(session->shm);
		nbl_shm_free(session->shm);
		session->shm = NULL;
		break;
	}

	nbl_stm_add_int(session->ack, cmd);
	nbl_chn_write_stm(chn, session->ack);

	return NBL_NO_ERROR;

get_cmd_failed:
	return rc;
}

static void _srv_a_close_handler(nbl_chn_t *chn) {
	session_a_t *session = nbl_chn_get_priv(chn);
	if (session) {
		if (session->buf) {
			free(session->buf);
		}

		if (session->shm) {
			nbl_shm_free(session->shm);
			session->shm = NULL;
			session->shm_base = NULL;
		}

		if (session->ack) {
			nbl_stm_destroy(session->ack);
			session->ack = NULL;
		}

		free(session);
	}
	NBL_LOGI("Simple HEE sample service A close handler invoked.\n");
}

typedef struct session_b {
	char *buf;
} session_b_t;

static int _srv_b_connect_handler(nbl_srv_t *srv, nbl_uuid_t peer_uuid) {
	NBL_LOGI("Simple HEE sample service B connect handler invoked.\n");
	return NBL_NO_ERROR;
}

static void _srv_b_open_handler(nbl_chn_t *chn) {
	session_b_t *session = calloc(1, sizeof(session_b_t));
	session->buf = malloc(256);
	nbl_chn_set_priv(chn, session);
	NBL_LOGI("Simple HEE sample service B open handler invoked.\n");
}

static int _srv_b_buf_handler(nbl_chn_t *chn, void *msg_buf, size_t msg_size) {
	int rc;
	session_b_t *session = nbl_chn_get_priv(chn);
	memcpy(session->buf, msg_buf, msg_size);
	NBL_LOGI("Service B receive message (%s)\n", session->buf);

	rc = nbl_chn_write_buf(chn, msg_buf, msg_size);
	if (rc != msg_size) {
		NBL_LOGE("Service B response client failed (%d).\n", rc);
		return NBL_ERR_FAULT;
	}

	return NBL_NO_ERROR;
}

static void _srv_b_close_handler(nbl_chn_t *chn) {
	session_b_t *session = nbl_chn_get_priv(chn);
	if (session) {
		if (session->buf) {
			free(session->buf);
			session->buf = NULL;
		}

		free(session);
	}
	NBL_LOGI("Simple HEE sample service B close handler invoked.\n");
}

static nbl_srv_desc_t _services[] = {
		{
				.name = SRV_A_NAME,
				.flags = NBL_SRV_FLAG_SCOPE_REE,
				.buf_num = NBL_SRV_DEFAULT_BUF_NUM,
				.buf_size = NBL_SRV_MAX_BUF_SIZE,
				.conn_handler = _srv_a_connect_handler,
				.open_handler = _srv_a_open_handler,
				.stm_handler = _srv_a_stm_handler,
				.buf_handler = NULL,
				.close_handler = _srv_a_close_handler
		},
		{
				.name = SRV_B_NAME,
				.flags = NBL_SRV_FLAG_SCOPE_REE,
				.buf_num = NBL_SRV_DEFAULT_BUF_NUM,
				.buf_size = NBL_SRV_MAX_BUF_SIZE,
				.conn_handler = _srv_b_connect_handler,
				.open_handler = _srv_b_open_handler,
				.stm_handler = NULL,
				.buf_handler = _srv_b_buf_handler,
				.close_handler = _srv_b_close_handler
		},
};


int main() {
	int rc = NBL_NO_ERROR;
	nbl_srv_t *srv = NULL;
	nbl_app_t *app = nbl_app_create();
	unsigned i;

	NBL_LOGI("Simple HEE starts.\n");

	for (i = 0; i < sizeof(_services) / sizeof(nbl_srv_desc_t); i++) {
		rc = nbl_srv_create(&_services[i], NULL);
		if (rc != NBL_NO_ERROR) {
			NBL_LOGE("Create service %s failed!", _services[i].name);
			goto create_service_test_failed;
		}

		rc = nbl_app_reg_srv(app, _services[i].srv);
		if (rc != NBL_NO_ERROR) {
			NBL_LOGE("Register service %s failed!", _services[i].name);
			goto reg_service_test_failed;
		}
	}

	nbl_app_main_loop(app);

reg_service_test_failed:
create_service_test_failed:
	for (i = 0; i < sizeof(_services) / sizeof(nbl_srv_desc_t); i++) {
		nbl_srv_destroy(_services[i].srv);
	}
	nbl_app_destroy(app);
	app = NULL;
	return rc;
}
