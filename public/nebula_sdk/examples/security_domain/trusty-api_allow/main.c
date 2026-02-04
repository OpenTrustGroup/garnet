// Copyright 2019 GoldenRiver Technology Co., Ltd.

#include <stdio.h>

#include <uapi/err.h>
#include <trusty_std.h>
#include <trusty/sys/types.h>

#define LOG_TAG "trusty-api"

# define MAX_ECHO_MSG_SIZE 64

int main(int argc, char** argv) {
	int rc;
	const char *path = "test.call-trusty-api";

	printf("call trusty-api with _trusty_port_create\n");

	/* create control port and just wait on it */
	rc = _trusty_port_create(path, 1, MAX_ECHO_MSG_SIZE,
			         IPC_PORT_ALLOW_NS_CONNECT);
	if (rc < 0) {
		if (rc == ERR_ACCESS_DENIED) {
			printf("Failed. Access denied\n");
		} else {
			printf("Failed. unknown error=%d\n", rc);
		}
	} else {
		printf("Success.\n");
		_trusty_close((handle_t)rc);
	}

	return 0;
}
