/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>

#include <trusty/tipc.h>

#define LOG_TAG "memref_ree_client"

#define TIPC_DEFAULT_DEVNAME "/dev/trusty-ipc-dev0"

#define MAX_PORT_BUF_SIZE  4096    /* max size of per port buffer    */

#define ACK_OK "ok"
#define CMD_SEND_MEMREF "cmd_send_memref"
#define MSG_WITH_TIPC "This message is sent with tipc."
#define MSG_WITH_MEMREF "This message is sent with memref."
#define MSG_ACK_WITH_MEMREF "This message is returned by HEE with memref."

static int send_and_receive_msg_with_tipc() {
	uint i;
	ssize_t rc;
	int err = 0;
	size_t msg_len;
	int fd = -1;
	int retry_count = 10;
	char tx_buf[MAX_PORT_BUF_SIZE];
	char rx_buf[MAX_PORT_BUF_SIZE];

	while (fd < 0 && retry_count > 0) {
		printf("Connecting to memref server...\n");
		fd = tipc_connect(TIPC_DEFAULT_DEVNAME,
				"com.grt.nbl.memref.sample.server");
		if (fd < 0) {
			sleep(1);
		}
		retry_count--;
	}

	if (fd < 0) {
		fprintf(stderr, "Failed to connect to service\n");
		return fd;
	}

	msg_len = strlen(MSG_WITH_TIPC) + 1;
	strcpy(tx_buf, MSG_WITH_TIPC);

	rc = write(fd, tx_buf, msg_len);
	if ((size_t) rc != msg_len) {
		fprintf(stderr, "Send message with tipc failed!");
		err = -1;
		goto write_msg_failed;
	}

	rc = read(fd, rx_buf, msg_len);
	if (rc < 0) {
		fprintf(stderr, "Read message with tipc failed!");
		err = -1;
		goto read_msg_failed;
	}

	if ((size_t) rc != msg_len) {
		fprintf(stderr, "data truncated (%zu vs. %zu)\n", rc, msg_len);
		err = -1;
		goto len_check_failed;
	}

	if (memcmp(tx_buf, rx_buf, (size_t) rc)) {
		fprintf(stderr, "data mismatch\n");
		err = -1;
	}

len_check_failed:
write_msg_failed:
read_msg_failed:
	tipc_close(fd);

	return err;
}

static int send_and_receive_msg_with_memref() {
	uint i;
	ssize_t rc;
	int err = 0;
	size_t cmd_len;
	size_t ack_len;
	int fd = -1;
	int retry_count = 10;
	char tx_buf[MAX_PORT_BUF_SIZE];
	char rx_buf[MAX_PORT_BUF_SIZE];
	struct tipc_memref memrefs[1];
	struct iovec iovs[1] = {{tx_buf, 0}};
	void *shrmem_base = NULL;

	while (fd < 0 && retry_count > 0) {
		printf("Connecting to memref server...\n");
		fd = tipc_connect(TIPC_DEFAULT_DEVNAME,
				"com.grt.nbl.memref.sample.server");
		if (fd < 0) {
			sleep(1);
		}
		retry_count--;
	}

	if (fd < 0) {
		fprintf(stderr, "Failed to connect to service\n");
		return fd;
	}

	cmd_len = strlen(CMD_SEND_MEMREF) + 1;
	strcpy(tx_buf, CMD_SEND_MEMREF);
	iovs[0].iov_len = cmd_len;

	rc = tipc_memref_alloc(&memrefs[0], 4096, TIPC_MEMREF_PERM_RW);
	if (rc) {
		fprintf(stderr, "Alloc memref failed!");
		err = -1;
		goto memref_alloc_failed;
	}

	shrmem_base = (void *)memrefs[0].shr_base;
	strcpy(shrmem_base, MSG_WITH_MEMREF);

	rc = tipc_send_msg(fd, iovs, 1, memrefs, 1);
	if ((size_t) rc != cmd_len) {
		fprintf(stderr, "Send message with tipc failed!");
		err = -1;
		goto send_msg_failed;
	}

	ack_len = strlen(ACK_OK);
	rc = read(fd, rx_buf, ack_len);
	if (rc < 0) {
		fprintf(stderr, "Read message with tipc failed!");
		err = -1;
		goto read_msg_failed;
	}

	if ((size_t) rc != ack_len) {
		fprintf(stderr, "data truncated (%zu vs. %zu)\n", rc, ack_len);
		err = -1;
		goto len_check_failed;
	}

	if (memcmp(ACK_OK, rx_buf, (size_t) rc)
			|| strcmp(MSG_ACK_WITH_MEMREF, shrmem_base)) {
		fprintf(stderr, "data mismatch\n");
		err = -1;
	}

len_check_failed:
read_msg_failed:
send_msg_failed:
	tipc_memref_free(&memrefs[0]);
memref_alloc_failed:
	tipc_close(fd);

	return err;
}

int main(int argc, char **argv) {
	int rc = 0;

	printf("Welcome to memref REE client.\n");

	rc |= send_and_receive_msg_with_tipc();
	rc |= send_and_receive_msg_with_memref();

	if (!rc) {
		printf(LOG_TAG " PASSED\n");
	} else {
		printf(LOG_TAG " FAILED\n");
	}

	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
