#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sec_client.h>
#include <nbl_fwk_ree/service.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/objects.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#define SHM_SEND_THRESHOLD 4000
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

typedef struct sec_conn {
	nbl_chn_t *chn;
	RSA *rsa_pub;
	uint8_t session_key[SESSION_KEY_SIZE];
	uint8_t iv[SESSION_KEY_SIZE];
} sec_conn_t;
