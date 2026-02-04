#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct sec_conn sec_conn_t;

typedef void (*on_client_connect_t)(sec_conn_t *conn);
typedef void (*on_handle_msg_t)(sec_conn_t *conn);
typedef void (*on_client_disconnect_t)(sec_conn_t *conn);

typedef struct sec_srv_desc {
	const char *name;
	on_client_connect_t on_conn_handler;
	on_handle_msg_t msg_handler;
	on_client_disconnect_t on_disconn_handler;
} sec_srv_desc_t;

int sec_srv_start(sec_srv_desc_t *desc);
int sec_conn_receive(sec_conn_t *conn, uint8_t *buf, size_t *out_size,
		size_t max_size);
int sec_conn_reply(sec_conn_t *conn, uint8_t *buf, size_t size);
void sec_conn_set_priv(sec_conn_t *conn, void *priv);
void *sec_conn_get_priv(sec_conn_t *conn);
