#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct sec_conn sec_conn_t;

sec_conn_t *secc_connect(const char *srv_name);
int secc_call(sec_conn_t *conn, uint8_t *snd_buf, size_t snd_size,
		uint8_t *rcv_buf, size_t *rcv_size, size_t max_rcv_size);
void secc_disconnect(sec_conn_t *conn);
