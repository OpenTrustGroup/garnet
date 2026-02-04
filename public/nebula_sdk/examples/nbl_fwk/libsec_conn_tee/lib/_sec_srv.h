#pragma once
#include <sec_srv.h>
#include <nbl_fwk/service.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/ossl_typ.h>
#include <openssl/buffer.h>
#include <openssl/pem.h>

#define SHM_SEND_THRESHOLD 4000
#define RSA_KEY_BITS 2048
#define PUB_KEY_FILENAME "seccon.pub"
#define PRIV_KEY_FILENAME "seccon.priv"
#define RSA_KEY_MAX_SIZE 2048
#define SESSION_KEY_SIZE 16

enum {
	SECS_CMD_BEGIN_CALL = 1,
	SECC_CMD_ACK_BEGIN_CALL,

	SECS_CMD_PREPARE_SEND_MSG,
	SECC_CMD_ACK_PREPARE_SEND_MSG,

	SECS_CMD_TRANSFER_MSG,
	SECC_CMD_ACK_TRANSFER_MSG,

	SECS_CMD_TRANSFER_COMPLETE,
	SECC_CMD_ACK_TRANSFER_COMPLETE,

	SECC_CMD_PREPARE_REPLY_MSG,
	SECS_CMD_ACK_PREPARE_REPLY_MSG,

	SECC_CMD_TRANSFER_MSG,
	SECS_CMD_ACK_TRANSFER_MSG,

	SECC_CMD_TRANSFER_COMPLETE,
	SECS_CMD_ACK_TRANSFER_COMPLETE,

	SECC_CMD_END_CALL,

	SECC_CMD_ERROR,

	SECS_CMD_GET_PUB,
	SECC_CMD_ACK_GET_PUB,

	SECS_CMD_SESSION_KEY_INIT,
	SECC_CMD_ACK_SESSION_KEY_INIT
};


typedef struct sec_srv {
	on_client_connect_t on_conn_handler;
	on_handle_msg_t msg_handler;
	on_client_disconnect_t on_disconn_handler;
	nbl_srv_t *srv;
	nbl_app_t *app;
} sec_srv_t;

typedef struct sec_conn {
	nbl_chn_t *chn;
	RSA *rsa_priv;
	void *priv;
	uint8_t session_key[SESSION_KEY_SIZE];
	uint8_t iv[SESSION_KEY_SIZE];
} sec_conn_t;

sec_srv_t *sec_srv_create(sec_srv_desc_t *desc);
void sec_srv_destroy(sec_srv_t *srv);
