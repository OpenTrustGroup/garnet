#include <stdlib.h>
#include <sec_client.h>
#include <assert.h>

#include "_sec_client.h"

#define LOG_TAG "sec_client"

static RSA *_create_pub_rsa(const char *pub_key) {
	BIO* bio = BIO_new(BIO_s_mem());
	EVP_PKEY* evp_key = NULL;
	RSA *rsa = NULL;

	BIO_write(bio, pub_key, strlen((const char *)pub_key));
	evp_key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
	if (evp_key == NULL) {
		goto failed;
	}

	rsa = EVP_PKEY_get1_RSA(evp_key);

failed:
	EVP_PKEY_free(evp_key);
	BIO_free(bio);

	return rsa;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static void _hexdump8(const void *ptr, size_t len)
{
	unsigned long address = (unsigned long)ptr;
	size_t count;
	size_t i;

	for (count = 0 ; count < len; count += 16) {
		printf("0x%08lx: ", address);
		for (i=0; i < MIN(len - count, 16); i++) {
			printf("0x%02hhx ", *(const uint8_t *)(address + i));
		}
		printf("\n");
		address += 16;
	}
}

static void _print_error(nbl_stm_t *stm, int cmd) {
	int rc = NBL_NO_ERROR;
	char err_msg[256];
	int msg_size = 0;
	if (cmd == SECC_CMD_ERROR) {
		nbl_stm_next_int(stm, &rc);
		nbl_stm_next_buf(stm, &msg_size, err_msg);
		assert(msg_size <= 256);
		NBL_LOGE("Receive TEE error report rc (%d) msg:%s\n", rc, err_msg);
	} else {
		NBL_LOGE("Unknown cmd (%d)\n", cmd);
	}
}


static int _aes_encrypt(uint8_t *iv, uint8_t *key, uint8_t *from, int from_len,
		uint8_t *to, int *to_len) {
	int rc;
	EVP_CIPHER_CTX *ctx = NULL;
	int enc_len = 0;
	int last_len = 0;

	if (!iv || !key || !from || !to || !to_len) {
		return NBL_ERR_INVALID_ARGS;
	}

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx) {
		NBL_LOGE("Create cipher context failed.\n");
		rc = NBL_ERR_NO_MEMORY;
		goto failed;
	}

	rc = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Init cipher context failed (%d).\n", rc);
		goto failed;
	}

	rc = EVP_EncryptUpdate(ctx, to, &enc_len, from, from_len);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Encrypt update failed (%d).\n", rc);
		goto failed;
	}

	rc = EVP_EncryptFinal_ex(ctx, to + enc_len, &last_len);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Encrypt final failed (%d).\n", rc);
		goto failed;
	}
	enc_len += last_len;

	if (to_len) {
		*to_len = enc_len;
	}

	rc = NBL_NO_ERROR;

failed:
	EVP_CIPHER_CTX_free(ctx);
	return rc;
}

static int _aes_decrypt(uint8_t *iv, uint8_t *key, uint8_t *from, int from_len,
		uint8_t *to, int *to_len, size_t max_size) {
	int rc;
	EVP_CIPHER_CTX *ctx = NULL;
	int dec_len = 0;
	int last_len = 0;
	int block_size = 0;
	int in_len = 0;
	int out_len = 0;
	uint8_t *last_block = NULL;

	if (!iv || !key || !from || !to || !to_len) {
		return NBL_ERR_INVALID_ARGS;
	}

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx) {
		NBL_LOGE("Create cipher context failed.\n");
		rc = NBL_ERR_NO_MEMORY;
		goto failed;
	}

	rc = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Init cipher context failed (%d).\n", rc);
		goto failed;
	}

	block_size = EVP_CIPHER_CTX_block_size(ctx);
	in_len = from_len - block_size;

	if (in_len > max_size) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Exceed max buffer size (%zd).\n", max_size);
		goto failed;
	}

	rc = EVP_DecryptUpdate(ctx, to, &out_len, from, in_len);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Decode update failed (%d).\n", rc);
		goto failed;
	}

	dec_len += out_len;

	last_block = (uint8_t *)calloc(2, block_size);

	/* To void buffer overflow, here update last block alone. */
	rc = EVP_DecryptUpdate(ctx, last_block, &out_len, from + in_len,
			block_size);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Decode update failed (%d).\n", rc);
		goto failed;
	}

	last_len += out_len;

	rc = EVP_DecryptFinal_ex(ctx, last_block + last_len, &out_len);
	if (rc != 1) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Decode final failed (%d).\n", rc);
		goto failed;
	}

	last_len += out_len;

	if (dec_len + last_len > max_size) {
		rc = NBL_ERR_GENERIC;
		NBL_LOGE("Exceed max buffer size (%zd).\n", max_size);
		goto failed;
	}

	if (last_len) {
		memcpy(to + dec_len, last_block, last_len);
	}

	dec_len += last_len;

	if (to_len) {
		*to_len = dec_len;
	}

	rc = NBL_NO_ERROR;

failed:
	if (last_block) {
		free(last_block);
		last_block = NULL;
	}

	EVP_CIPHER_CTX_free(ctx);

	return rc;
}

static int _calc_aes_enc_max_size(uint8_t *iv, uint8_t *key, int size) {
	int result = 0;
	static int block_size = 0;

	if (!block_size) {
		EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
		EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
		block_size = EVP_CIPHER_CTX_block_size(ctx);
		EVP_CIPHER_CTX_free(ctx);
	}

	result = size + block_size;

	return result;
}

static void report_error(nbl_chn_t *chn, nbl_stm_t *stm, int err) {
	nbl_stm_reset(stm);
	nbl_stm_add_int(stm, SECC_CMD_ERROR);
	nbl_stm_add_int(stm, err);
	nbl_chn_write_stm(chn, stm);
}

sec_conn_t *secc_connect(const char *srv_name) {
	int rc;
	sec_conn_t *conn = NULL;
	nbl_chn_t *chn = NULL;
	RSA *rsa_pub = NULL;
	nbl_stm_t *rx = nbl_stm_create();
	nbl_stm_t *tx = nbl_stm_create();
	char pub_buf[RSA_KEY_MAX_SIZE];
	int pub_size = 0;
	uint8_t *ori_buf = NULL;
	uint8_t *enc_buf = NULL;
	int rsa_size = 0;
	int data_size = 0;
	int enc_size = 0;
	int cmd = 0;

	rc = nbl_chn_open(srv_name, 0, &chn);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Connect to %s failed.\n", srv_name);
		goto connect_failed;
	}

	conn = (sec_conn_t *)calloc(1, sizeof(sec_conn_t));
	if (!conn) {
		NBL_LOGE("Alloc connection context failed.\n");
		goto alloc_conn_failed;
	}

	conn->chn = chn;

	/* Get public key from TA. */
	nbl_stm_add_int(tx, SECS_CMD_GET_PUB);
	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECS_CMD_GET_PUB failed (%d).\n", rc);
		goto get_pub_failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_ACK_GET_PUB) {
		_print_error(rx, cmd);
		goto ack_get_pub_failed;
	}

	nbl_stm_next_buf(rx, &pub_size, pub_buf);
	assert(pub_size < RSA_KEY_MAX_SIZE);

	rsa_pub = _create_pub_rsa(pub_buf);
	if (!rsa_pub) {
		NBL_LOGE("Init create pub failed.\n");
		goto create_pub_failed;
	}

	conn->rsa_pub = rsa_pub;

	/* Generate session key and iv. */
	rc = RAND_bytes(conn->session_key, sizeof(conn->session_key));
	if (rc != 1) {
		NBL_LOGE("Generate session key failed (%d).\n", rc);
		goto gen_session_key_failed;
	}

	rc = RAND_bytes(conn->iv, sizeof(conn->iv));
	if (rc != 1) {
		NBL_LOGE("Generate iv failed (%d).\n", rc);
		goto gen_iv_failed;
	}

	/* Encrypt session key and iv. */
	rsa_size = RSA_size(rsa_pub);
	ori_buf = (uint8_t *)calloc(1, rsa_size);
	enc_buf = (uint8_t *)calloc(1, rsa_size);

	memcpy(ori_buf, conn->session_key, sizeof(conn->session_key));
	data_size += sizeof(conn->session_key);

	memcpy(ori_buf + sizeof(conn->session_key), conn->iv, sizeof(conn->iv));
	data_size += sizeof(conn->iv);

	rc = RSA_public_encrypt(data_size, ori_buf, enc_buf, rsa_pub,
			RSA_PKCS1_PADDING);
	if (rc < 0) {
		NBL_LOGE("Encrypt failed (%d).\n", rc);
		goto encrypt_failed;
	}

	enc_size = rc;

	/* Send encrypted session key to TA */
	nbl_stm_add_int(tx, SECS_CMD_SESSION_KEY_INIT);
	nbl_stm_add_buf(tx, enc_size, enc_buf);
	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECS_CMD_GET_PUB failed (%d).\n", rc);
		goto send_key_failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_ACK_SESSION_KEY_INIT) {
		_print_error(rx, cmd);
		goto init_key_failed;
	}

	if (ori_buf) {
		free(ori_buf);
	}
	if (enc_buf) {
		free(enc_buf);
	}

	nbl_stm_destroy(rx);
	nbl_stm_destroy(tx);

	return conn;

init_key_failed:
send_key_failed:
encrypt_failed:
	if (ori_buf) {
		free(ori_buf);
	}
	if (enc_buf) {
		free(enc_buf);
	}
gen_iv_failed:
gen_session_key_failed:
	RSA_free(rsa_pub);
create_pub_failed:
ack_get_pub_failed:
get_pub_failed:
alloc_conn_failed:
	nbl_chn_close(chn);
connect_failed:
	nbl_stm_destroy(rx);
	nbl_stm_destroy(tx);
	return NULL;
}

static int _secc_send(sec_conn_t *conn, uint8_t *snd_buf, size_t snd_size) {
	int rc = NBL_ERR_GENERIC;
	nbl_stm_t *rx = nbl_stm_create();
	nbl_stm_t *tx = nbl_stm_create();
	int cmd = 0;
	int use_shm = 0;
	nbl_shm_t *shm = NULL;
	void *shm_base = NULL;
	int enc_size = 0;
	void *enc_buf = NULL;

	nbl_stm_add_int(tx, SECS_CMD_PREPARE_SEND_MSG);
	enc_size = _calc_aes_enc_max_size(conn->iv, conn->session_key, snd_size);
	if (enc_size > SHM_SEND_THRESHOLD) {
		use_shm = 1;
		rc = nbl_shm_alloc(conn->chn, enc_size, 0, &shm, &shm_base);
		if (rc != NBL_NO_ERROR) {
			NBL_LOGE("Alloc shm memory size (%d) failed (%d).\n", enc_size, rc);
			report_error(conn->chn, tx, rc);
			goto failed;
		}
		nbl_stm_add_int(tx, use_shm);
		nbl_stm_add_shm(tx, shm);
	} else {
		use_shm = 0;
		nbl_stm_add_int(tx, use_shm);
	}

	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECS_CMD_PREPARE_SEND_MSG failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_ACK_PREPARE_SEND_MSG) {
		_print_error(rx, cmd);
		goto failed;
	}

	nbl_stm_add_int(tx, SECS_CMD_TRANSFER_MSG);
	if (shm_base) {
		enc_buf = shm_base;
	} else {
		nbl_stm_add_buf_ptr(tx, enc_size, &enc_buf);
	}

	rc = _aes_encrypt(conn->iv, conn->session_key, snd_buf, snd_size, enc_buf,
			&enc_size);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Encrypt msg failed (%d).\n", rc);
		report_error(conn->chn, tx, rc);
		goto failed;
	}

	nbl_stm_add_int(tx, enc_size);

	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECS_CMD_TRANSFER_MSG failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_ACK_TRANSFER_MSG) {
		_print_error(rx, cmd);
		goto failed;
	}

	nbl_stm_add_int(tx, SECS_CMD_TRANSFER_COMPLETE);
	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECS_CMD_TRANSFER_COMPLETE failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_ACK_TRANSFER_COMPLETE) {
		_print_error(rx, cmd);
		goto failed;
	}

failed:
	nbl_shm_free(shm);
	nbl_stm_destroy(rx);
	nbl_stm_destroy(tx);
	return rc;
}

static int _secc_receive(sec_conn_t *conn, uint8_t *rcv_buf, size_t *rcv_size,
		size_t max_rcv_size) {
	int rc = NBL_ERR_GENERIC;
	nbl_stm_t *stm = nbl_stm_create();
	int complete = 0;
	int cmd = 0;
	nbl_shm_t *shm = NULL;
	void *shm_base = NULL;
	int use_shm = 0;
	int enc_size = 0;
	int dec_size = 0;
	int read_size = 0;
	void *enc_buf = NULL;
	void *dec_buf = NULL;

	while (!complete) {
		rc = nbl_chn_read_stm(conn->chn, stm, NBL_TIMEOUT_INFINITY);
		if (rc != NBL_NO_ERROR) {
			NBL_LOGE("Read stream failed (%d).\n", rc);
			rc = NBL_ERR_IO;
			goto failed;
		}

		cmd = 0;
		nbl_stm_next_int(stm, &cmd);

		switch(cmd) {
		case SECC_CMD_PREPARE_REPLY_MSG:
			nbl_stm_next_int(stm, &enc_size);

			nbl_stm_reset(stm);
			nbl_stm_add_int(stm, SECS_CMD_ACK_PREPARE_REPLY_MSG);

			if (enc_size > SHM_SEND_THRESHOLD) {
				use_shm = 1;
				rc = nbl_shm_alloc(conn->chn, enc_size, 0, &shm, &shm_base);
				if (rc != NBL_NO_ERROR) {
					NBL_LOGE("Alloc shm memory size (%d) failed (%d).\n",
							enc_size, rc);
					report_error(conn->chn, stm, rc);
					goto failed;
				}
				nbl_stm_add_int(stm, use_shm);
				rc = nbl_stm_add_shm(stm, shm);
				if (rc != NBL_NO_ERROR) {
					NBL_LOGE("Add shm memory failed (%d).\n", rc);
					report_error(conn->chn, stm, rc);
					goto failed;
				}
			} else {
				use_shm = 0;
				nbl_stm_add_int(stm, use_shm);
			}

			nbl_chn_write_stm(conn->chn, stm);
			break;
		case SECC_CMD_TRANSFER_MSG:
			if (shm_base) {
				enc_buf = shm_base;
				read_size = enc_size;
			} else {
				nbl_stm_next_buf_ptr(stm, &read_size, &enc_buf);
			}

			nbl_stm_next_int(stm, &enc_size);

			rc = _aes_decrypt(conn->iv, conn->session_key, enc_buf, enc_size,
					rcv_buf, &dec_size, max_rcv_size);
			if (rc != NBL_NO_ERROR) {
				NBL_LOGE("Decode msg failed (%d).\n", rc);
				report_error(conn->chn, stm, rc);
				goto failed;
			}

			nbl_stm_reset(stm);
			nbl_stm_add_int(stm, SECS_CMD_ACK_TRANSFER_MSG);
			nbl_chn_write_stm(conn->chn, stm);
			break;
		case SECC_CMD_TRANSFER_COMPLETE:
			if (rcv_size) {
				*rcv_size = dec_size;
			}
			complete = 1;
			nbl_stm_reset(stm);
			nbl_stm_add_int(stm, SECS_CMD_ACK_TRANSFER_COMPLETE);
			nbl_chn_write_stm(conn->chn, stm);
			break;
		default:
			_print_error(stm, cmd);
			rc = NBL_ERR_CMD_UNKNOWN;
			complete = 1;
			break;
		}
	}

failed:
	nbl_shm_free(shm);
	nbl_stm_destroy(stm);

	return rc;
}

int secc_call(sec_conn_t *conn, uint8_t *snd_buf, size_t snd_size,
		uint8_t *rcv_buf, size_t *rcv_size, size_t max_rcv_size) {
	int rc = NBL_ERR_GENERIC;
	nbl_stm_t *rx = NULL;
	nbl_stm_t *tx = NULL;
	int cmd = 0;

	if (!conn || !snd_buf || !rcv_buf) {
		return NBL_ERR_INVALID_ARGS;
	}

	if (!conn->chn || !conn->rsa_pub) {
		return NBL_ERR_OBJECT_DESTROYED;
	}

	if (!snd_size) {
		return NBL_NO_ERROR;
	}

	rx = nbl_stm_create();
	tx = nbl_stm_create();

	nbl_stm_add_int(tx, SECS_CMD_BEGIN_CALL);
	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECS_CMD_BEGIN_CALL failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_ACK_BEGIN_CALL) {
		_print_error(rx, cmd);
		goto failed;
	}

	rc = _secc_send(conn, snd_buf, snd_size);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Send message failed (%d).\n", rc);
		goto failed;
	}

	rc = _secc_receive(conn, rcv_buf, rcv_size, max_rcv_size);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Receive message failed (%d).\n", rc);
		goto failed;
	}

	rc = nbl_chn_read_stm(conn->chn, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Read stream failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECC_CMD_END_CALL) {
		_print_error(rx, cmd);
		goto failed;
	}

failed:
	nbl_stm_destroy(rx);
	nbl_stm_destroy(tx);
	return rc;
}

void secc_disconnect(sec_conn_t *conn) {
	if (conn) {
		nbl_chn_close(conn->chn);
		RSA_free(conn->rsa_pub);
		conn->chn = NULL;
		conn->rsa_pub = NULL;
		memset(conn->session_key, 0, sizeof(conn->session_key));
		memset(conn->iv, 0, sizeof(conn->iv));
		free(conn);
	}
}
