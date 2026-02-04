#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sec_srv.h>
#include <nbl_fwk/stream.h>
#include <lib/storage/storage.h>

#include "_sec_srv.h"

#define LOG_TAG "sec_srv"

#define LEAK_TEST 0

#if LEAK_TEST
void nbl_enable_trace_heap(void);
extern void nbl_start_trace_heap(void);
extern int nbl_end_trace_heap(int try_fix);
#endif

static sec_srv_t *_srv;

static char _key_pub[RSA_KEY_MAX_SIZE];
static char _key_priv[RSA_KEY_MAX_SIZE];

static RSA *_create_priv_rsa(const char *priv_key) {
	BIO* bio = BIO_new(BIO_s_mem());
	EVP_PKEY* evp_key = NULL;
	RSA *rsa = NULL;

	BIO_write(bio, priv_key, strlen((const char *)priv_key));
	evp_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
	if (evp_key == NULL) {
		goto failed;
	}

	rsa = EVP_PKEY_get1_RSA(evp_key);

failed:
	EVP_PKEY_free(evp_key);
	BIO_free(bio);

	return rsa;
}

static void _srv_open_handler(nbl_chn_t *chn) {
#if LEAK_TEST
	nbl_start_trace_heap();
#endif
	sec_conn_t *conn = NULL;
	RSA *rsa_priv = NULL;

	conn = (sec_conn_t *)calloc(1, sizeof(sec_conn_t));
	if (!conn) {
		NBL_LOGE("Can not alloc connection context.\n");
		goto failed;
	}

	rsa_priv = _create_priv_rsa(_key_priv);
	if (!rsa_priv) {
		NBL_LOGE("Can not create RSA context.\n");
		goto failed;
	}

	conn->chn = chn;
	conn->rsa_priv = rsa_priv;

	nbl_chn_set_priv(chn, conn);

	if (_srv && _srv->on_conn_handler) {
		_srv->on_conn_handler(conn);
	}

	return;

failed:
	if (conn) {
		conn->chn = NULL;
		conn->priv = NULL;
		conn->rsa_priv = NULL;
		free(conn);
	}
	RSA_free(rsa_priv);
	nbl_chn_close(chn);
}

static void report_error(nbl_chn_t *chn, nbl_stm_t *stm, int err, char *err_msg) {
	nbl_stm_reset(stm);
	nbl_stm_add_int(stm, SECC_CMD_ERROR);
	nbl_stm_add_int(stm, err);
	nbl_stm_add_buf(stm, strlen(err_msg) + 1, err_msg);
	nbl_chn_write_stm(chn, stm);
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

static int _srv_stm_handler(nbl_chn_t *chn, nbl_stm_t *stm) {
	int rc;
	int cmd = 0;
	sec_conn_t *conn = (sec_conn_t *) nbl_chn_get_priv(chn);
	if (!conn) {
		report_error(chn, stm, NBL_ERR_BAD_STATE,
						"No connection context found!\n");
		return NBL_NO_ERROR;
	}

	// Read all commands here.
	nbl_stm_next_int(stm, &cmd);

	switch (cmd) {
	case SECS_CMD_BEGIN_CALL: {
		// We will reuse stm and reset it here.
		nbl_stm_reset(stm);
		nbl_stm_add_int(stm, SECC_CMD_ACK_BEGIN_CALL);
		nbl_chn_write_stm(chn, stm);

		if (_srv && _srv->msg_handler) {
			_srv->msg_handler(conn);
		}

		nbl_stm_reset(stm);
		nbl_stm_add_int(stm, SECC_CMD_END_CALL);
		nbl_chn_write_stm(chn, stm);
		break;
	}
	case SECS_CMD_GET_PUB: {
		nbl_stm_reset(stm);
		nbl_stm_add_int(stm, SECC_CMD_ACK_GET_PUB);
		nbl_stm_add_buf(stm, strlen(_key_pub) + 1, _key_pub);
		nbl_chn_write_stm(chn, stm);
		break;
	}
	case SECS_CMD_SESSION_KEY_INIT: {
		int rsa_len = RSA_size(conn->rsa_priv);
		int read_size = 0;
		uint8_t *rsa_enc_buf = (uint8_t *)calloc(1, rsa_len);
		uint8_t *rsa_dec_buf = (uint8_t *)calloc(1, rsa_len);

		nbl_stm_next_buf(stm, &read_size, rsa_enc_buf);
		if (read_size > rsa_len) {
			free(rsa_enc_buf);
			free(rsa_dec_buf);
			report_error(chn, stm, NBL_ERR_BAD_LEN, "Message length wrong.\n");
			break;
		}

		rc = RSA_private_decrypt(rsa_len, rsa_enc_buf, rsa_dec_buf,
				conn->rsa_priv, RSA_PKCS1_PADDING);
		if (rc < 0) {
			free(rsa_enc_buf);
			free(rsa_dec_buf);
			report_error(chn, stm, NBL_ERR_INVALID_ARGS, "RSA decode error.\n");
			break;
		}

		memcpy(conn->session_key, rsa_dec_buf, SESSION_KEY_SIZE);
		memcpy(conn->iv, rsa_dec_buf + SESSION_KEY_SIZE, SESSION_KEY_SIZE);

		nbl_stm_reset(stm);
		nbl_stm_add_int(stm, SECC_CMD_ACK_SESSION_KEY_INIT);
		nbl_chn_write_stm(chn, stm);

		memset(rsa_enc_buf, 0, rsa_len);
		memset(rsa_dec_buf, 0, rsa_len);
		free(rsa_enc_buf);
		free(rsa_dec_buf);
		break;
	}
	default:
		report_error(chn, stm, NBL_ERR_BAD_STATE,
				"Client must send SECS_CMD_BEGIN_CALL first.\n");
	}

	return NBL_NO_ERROR;
}

static void _srv_close_handler(nbl_chn_t *chn) {
	sec_conn_t *conn = (sec_conn_t *) nbl_chn_get_priv(chn);
	if (conn) {
		if (_srv && _srv->on_disconn_handler) {
			_srv->on_disconn_handler(conn);
		}

		RSA_free(conn->rsa_priv);

		conn->chn = NULL;
		conn->priv = NULL;
		conn->rsa_priv = NULL;

		memset(conn->session_key, 0, sizeof(conn->session_key));
		memset(conn->iv, 0, sizeof(conn->iv));

		free(conn);
	}
#if LEAK_TEST
	nbl_end_trace_heap(0);
#endif
}

sec_srv_t *sec_srv_create(sec_srv_desc_t *desc) {
	int rc = NBL_NO_ERROR;
	sec_srv_t *srv = NULL;

	nbl_srv_desc_t d;
	d.name = desc->name;
	d.flags = NBL_SRV_FLAG_SCOPE_REE;
	d.buf_num = NBL_SRV_DEFAULT_BUF_NUM;
	d.buf_size = NBL_SRV_MAX_BUF_SIZE;
	d.conn_handler = NULL;
	d.open_handler = _srv_open_handler;
	d.stm_handler = _srv_stm_handler;
	d.buf_handler = NULL;
	d.close_handler = _srv_close_handler;
	d.srv = NULL;

	rc = nbl_srv_create(&d, NULL);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Create service %s failed!\n", d.name);
		goto create_srv_failed;
	}

	nbl_app_t *app = nbl_app_create();
	if (!app) {
		NBL_LOGE("Create app failed!\n");
		goto create_app_failed;
	}

	rc = nbl_app_reg_srv(app, d.srv);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Register service %s failed!\n", d.name);
		goto reg_srv_failed;
	}

	srv = (sec_srv_t *) calloc(1, sizeof(sec_srv_t));
	if (!srv) {
		NBL_LOGE("Alloc service context failed!\n");
		goto alloc_srv_failed;
	}

	srv->on_conn_handler = desc->on_conn_handler;
	srv->msg_handler = desc->msg_handler;
	srv->on_disconn_handler = desc->on_disconn_handler;
	srv->srv = d.srv;
	srv->app = app;

	return srv;

alloc_srv_failed:
reg_srv_failed:
	nbl_app_destroy(app);
create_app_failed:
	nbl_srv_destroy(d.srv);
create_srv_failed:
	return NULL;
}

static int _load_file(const char *filename, void *buf, int *size) {
	int rc;
	file_handle_t handle;
	storage_session_t session;
	storage_off_t file_size;

	rc = storage_open_session(&session, STORAGE_CLIENT_TD_PORT);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Open session failed (%d).\n", rc);
		rc = NBL_ERR_GENERIC;
		goto open_session_failed;
	}

	rc = storage_open_file(session, &handle, filename, 0, STORAGE_OP_COMPLETE);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Can not open %s\n", filename);
		rc = NBL_ERR_GENERIC;
		goto open_file_failed;
	}

	rc = storage_get_file_size(handle, &file_size);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Get file size failed (%d)\n", rc);
		rc = NBL_ERR_GENERIC;
		goto get_file_size_failed;
	}

	if (buf) {
		rc = storage_read(handle, 0, buf, file_size);
		if (rc != file_size) {
			NBL_LOGE("Read failed (%d)\n", rc);
			rc = NBL_ERR_GENERIC;
			goto read_file_failed;
		}
	}

	if (size) {
		*size = file_size;
	}

	rc = NBL_NO_ERROR;

read_file_failed:
get_file_size_failed:
	storage_close_file(handle);
open_file_failed:
	storage_close_session(session);
open_session_failed:
	return rc;
}

static int _save_file(const char *filename, const void *buf, int size) {
	int rc;
	file_handle_t handle;
	storage_session_t session;

	rc = storage_open_session(&session, STORAGE_CLIENT_TD_PORT);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Open session failed (%d).\n", rc);
		rc = NBL_ERR_GENERIC;
		goto open_session_failed;
	}

	rc = storage_open_file(session, &handle, filename,
			STORAGE_FILE_OPEN_CREATE | STORAGE_FILE_OPEN_TRUNCATE,
			STORAGE_OP_COMPLETE);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Can not open %s\n", filename);
		rc = NBL_ERR_GENERIC;
		goto open_file_failed;
	}

	rc = storage_write(handle, 0, buf, size, STORAGE_OP_COMPLETE);
	if (rc != size) {
		NBL_LOGE("Write failed (%d)\n", rc);
		rc = NBL_ERR_GENERIC;
		goto write_file_failed;
	}

	rc = NBL_NO_ERROR;

write_file_failed:
	storage_close_file(handle);
open_file_failed:
	storage_close_session(session);
open_session_failed:
	return rc;
}

static int _generate_keys() {
	int rc;
	RSA *rsa = RSA_new();
	BIGNUM *e = BN_new();
	BIO* bio_pub = BIO_new(BIO_s_mem());
	BIO* bio_priv = BIO_new(BIO_s_mem());
	int pub_len = 0;
	int priv_len = 0;
	void *pub_buf = NULL;
	void *priv_buf = NULL;

	rc = BN_set_word(e, RSA_F4);
	if (rc != 1) {
		NBL_LOGE("Set word failed (%d)\n", rc);
		rc = NBL_ERR_GENERIC;
		goto failed;
	}

	rc = RSA_generate_key_ex(rsa, RSA_KEY_BITS, e, NULL);
	if (rc != 1) {
		NBL_LOGE("Generate RSA keys failed (%d)\n", rc);
		rc = NBL_ERR_GENERIC;
		goto failed;
	}

	PEM_write_bio_RSA_PUBKEY(bio_pub, rsa);
	pub_len = BIO_pending(bio_pub);
	pub_buf = calloc(1, pub_len + 1);
	BIO_read(bio_pub, pub_buf, pub_len);

	PEM_write_bio_RSAPrivateKey(bio_priv, rsa, NULL, NULL, 0, NULL, NULL);
	priv_len = BIO_pending(bio_priv);
	priv_buf = calloc(1, priv_len + 1);
	BIO_read(bio_priv, priv_buf, priv_len);

	rc = _save_file(PUB_KEY_FILENAME, pub_buf, pub_len + 1);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Save pub failed.\n");
		rc = NBL_ERR_GENERIC;
		goto failed;
	}

	rc = _save_file(PRIV_KEY_FILENAME, priv_buf, priv_len + 1);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Save priv failed.\n");
		rc = NBL_ERR_GENERIC;
		goto failed;
	}

	NBL_LOGI("Generate keys success.\n");

	rc = NBL_NO_ERROR;

failed:
	if (pub_buf) {
		free(pub_buf);
	}
	if (priv_buf) {
		free(priv_buf);
	}
	BIO_free(bio_pub);
	BIO_free(bio_priv);
	BN_free(e);
	RSA_free(rsa);

	pub_buf = NULL;
	priv_buf = NULL;
	bio_pub = NULL;
	bio_priv = NULL;
	e = NULL;
	rsa = NULL;

	return rc;
}

static int _init_keys() {
	int rc;
	int size;

	rc = _load_file(PUB_KEY_FILENAME, NULL, &size);
	if (rc != NBL_NO_ERROR) {
		_generate_keys();
	}

	rc = _load_file(PUB_KEY_FILENAME, _key_pub, &size);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Load pub failed (%d).\n", rc);
		goto failed;
	}
	assert(size <= RSA_KEY_MAX_SIZE);

	rc = _load_file(PRIV_KEY_FILENAME, _key_priv, &size);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Load pub failed (%d).\n", rc);
		goto failed;
	}
	assert(size <= RSA_KEY_MAX_SIZE);

	NBL_LOGI("Init keys success.\n");

failed:
	return rc;
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

int sec_srv_start(sec_srv_desc_t *desc) {
#if LEAK_TEST
	nbl_enable_trace_heap();
#endif
	int rc = NBL_NO_ERROR;

	if (!desc) {
		return NBL_ERR_INVALID_ARGS;
	}

	rc = _init_keys();
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Init keys failed (%d).\n", rc);
		rc = NBL_ERR_GENERIC;
		goto init_keys_failed;
	}

	_srv = sec_srv_create(desc);
	if (!_srv) {
		NBL_LOGE("Create service failed.\n");
		rc = NBL_ERR_GENERIC;
		goto create_srv_failed;
	}

	rc = nbl_app_main_loop(_srv->app);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("Service stopped with error (%d).\n", rc);
		rc = NBL_ERR_GENERIC;
		goto main_loop_failed;
	}

main_loop_failed:
	sec_srv_destroy(_srv);
	_srv = NULL;
create_srv_failed:
init_keys_failed:
	return rc;
}

int sec_conn_receive(sec_conn_t *conn, uint8_t *buf, size_t *out_size,
		size_t max_size) {
	int rc = NBL_ERR_GENERIC;
	nbl_stm_t *stm = nbl_stm_create();
	int complete = 0;
	int cmd = 0;
	int enc_size = 0;
	int dec_size = 0;
	int read_size = 0;
	void *enc_buf = NULL;
	nbl_shm_t *shm = NULL;
	void *shm_base = NULL;
	int use_shm = 0;

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
		case SECS_CMD_PREPARE_SEND_MSG:
			nbl_stm_next_int(stm, &use_shm);
			if (use_shm) {
				rc = nbl_stm_next_shm(stm, &shm);
				if (rc != NBL_NO_ERROR) {
					report_error(conn->chn, stm, rc, "get shm failed\n");
					goto failed;
				}

				nbl_shm_map(shm, 0, &shm_base);
				if (rc != NBL_NO_ERROR) {
					report_error(conn->chn, stm, rc, "map shm failed\n");
					goto failed;
				}
			}
			nbl_stm_reset(stm);
			nbl_stm_add_int(stm, SECC_CMD_ACK_PREPARE_SEND_MSG);
			nbl_chn_write_stm(conn->chn, stm);
			break;
		case SECS_CMD_TRANSFER_MSG:
			if (shm_base) {
				size_t shm_size;
				nbl_shm_get_data_size(shm, &shm_size);
				enc_buf = shm_base;
				read_size = shm_size;
			} else {
				nbl_stm_next_buf_ptr(stm, &read_size, &enc_buf);
			}
			nbl_stm_next_int(stm, &enc_size);

			rc = _aes_decrypt(conn->iv, conn->session_key, enc_buf, enc_size,
					buf, &dec_size, max_size);
			if (rc != NBL_NO_ERROR) {
				report_error(conn->chn, stm, NBL_ERR_FAULT,
						"Decode msg failed\n");
				goto failed;
			}

			nbl_stm_reset(stm);
			nbl_stm_add_int(stm, SECC_CMD_ACK_TRANSFER_MSG);
			nbl_chn_write_stm(conn->chn, stm);
			break;
		case SECS_CMD_TRANSFER_COMPLETE:
			*out_size = dec_size;
			complete = 1;
			nbl_stm_reset(stm);
			nbl_stm_add_int(stm, SECC_CMD_ACK_TRANSFER_COMPLETE);
			nbl_chn_write_stm(conn->chn, stm);
			break;
		default:
			rc = NBL_ERR_CMD_UNKNOWN;
			complete = 1;
			report_error(conn->chn, stm, NBL_ERR_BAD_STATE,
							"Unknown cmd.\n");
			break;
		}
	}

failed:
	nbl_shm_free(shm);
	nbl_stm_destroy(stm);
	return rc;
}

int sec_conn_reply(sec_conn_t *conn, uint8_t *buf, size_t size) {
	int rc = NBL_ERR_GENERIC;
	nbl_stm_t *rx = nbl_stm_create();
	nbl_stm_t *tx = nbl_stm_create();
	int cmd = 0;
	int use_shm = 0;
	nbl_shm_t *shm = NULL;
	void *shm_base = NULL;
	void *enc_buf = NULL;
	int enc_size = 0;

	nbl_stm_add_int(tx, SECC_CMD_PREPARE_REPLY_MSG);

	enc_size = _calc_aes_enc_max_size(conn->iv, conn->session_key, size);
	nbl_stm_add_int(tx, enc_size);

	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECC_CMD_PREPARE_REPLY_MSG failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECS_CMD_ACK_PREPARE_REPLY_MSG) {
		NBL_LOGE("SECS_CMD_ACK_PREPARE_REPLY_MSG failed (%d).\n", cmd);
		goto failed;
	}

	nbl_stm_next_int(rx, &use_shm);
	if (use_shm) {
		size_t shm_size = 0;

		rc = nbl_stm_next_shm(rx, &shm);
		if (rc != NBL_NO_ERROR) {
			report_error(conn->chn, tx, rc, "get shm failed\n");
			goto failed;
		}

		nbl_shm_get_data_size(shm, &shm_size);
		if (shm_size != enc_size) {
			rc = NBL_ERR_NOT_ENOUGH_BUFFER;
			report_error(conn->chn, tx, rc, "insufficient shm\n");
			goto failed;
		}

		rc = nbl_shm_map(shm, 0, &shm_base);
		if (rc != NBL_NO_ERROR) {
			report_error(conn->chn, tx, rc, "map shm failed\n");
			goto failed;
		}
	}

	nbl_stm_add_int(tx, SECC_CMD_TRANSFER_MSG);
	if (shm_base) {
		enc_buf = shm_base;
	} else {
		rc = nbl_stm_add_buf_ptr(tx, enc_size, &enc_buf);
		if (rc != NBL_NO_ERROR) {
			report_error(conn->chn, tx, enc_size, "msg too long\n");
			goto failed;
		}
	}

	rc = _aes_encrypt(conn->iv, conn->session_key, buf, size, enc_buf,
				&enc_size);
	if (rc != NBL_NO_ERROR) {
		report_error(conn->chn, tx, rc, "encrypt msg failed\n");
		goto failed;
	}

	nbl_stm_add_int(tx, enc_size);

	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECC_CMD_TRANSFER_MSG failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECS_CMD_ACK_TRANSFER_MSG) {
		NBL_LOGE("SECS_CMD_ACK_TRANSFER_MSG failed (%d).\n", cmd);
		goto failed;
	}

	nbl_stm_add_int(tx, SECC_CMD_TRANSFER_COMPLETE);
	rc = nbl_chn_write_stm_sync(conn->chn, tx, rx, NBL_TIMEOUT_INFINITY);
	if (rc != NBL_NO_ERROR) {
		NBL_LOGE("SECC_CMD_TRANSFER_COMPLETE failed (%d).\n", rc);
		goto failed;
	}

	nbl_stm_next_int(rx, &cmd);
	if (cmd != SECS_CMD_ACK_TRANSFER_COMPLETE) {
		NBL_LOGE("SECS_CMD_ACK_TRANSFER_COMPLETE failed (%d).\n", cmd);
		goto failed;
	}

failed:
	nbl_shm_free(shm);
	nbl_stm_destroy(rx);
	nbl_stm_destroy(tx);
	return rc;
}

void sec_srv_destroy(sec_srv_t *srv) {
	if (srv) {
		nbl_app_unreg_srv(srv->srv);
		nbl_app_destroy(srv->app);
		srv->on_conn_handler = NULL;
		srv->msg_handler = NULL;
		srv->on_disconn_handler = NULL;
		srv->srv = NULL;
		srv->app = NULL;
		free(srv);
	}
}

void sec_conn_set_priv(sec_conn_t *conn, void *priv) {
	if (conn) {
		conn->priv = priv;
	}
}
void *sec_conn_get_priv(sec_conn_t *conn) {
	void *priv = NULL;

	if (conn) {
		priv = conn->priv;
	}

	return priv;
}
