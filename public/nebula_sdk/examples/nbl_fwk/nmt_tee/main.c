// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <nbl_fwk/nmt_tee.h>

static int _check_buffer(void *data, int size) {
	int i = 0;
	char *cur = (char *)data;
	int rc = 0;

	for (i = 0; i < size; i++) {
		if (cur[i] != i % 256) {
			rc = -1;
			printf("Check buffer failed (%d, %d).\n", (int)cur[i], i);
			break;
		}
	}

	return rc;
}

int main() {
	int rc = -1;
	int ta_slot = 1;
	void *data = NULL;
	int data_len = 0;

	/*
	 * The CA in examples/nbl_fwk/nmt_ree must be firstly run to inject data to
	 * this TA.
	 */

	/*
	 * Firstly gets injected data length.
	 */
	data_len = nbl_plt_load_inject_data(ta_slot, 0, NULL, 0, 0);

	printf("Injected data length is %x bytes.\n", data_len);
	if (data_len <= 0) {
		goto failed;
	}

	/*
	 * Then allocates a buffer to receive injected data.
	 */
	data = calloc(1, data_len);

	/*
	 * Finally loads injected data into the buffer.
	 */
	rc = nbl_plt_load_inject_data(ta_slot, 0, data, data_len, 0);

	printf("Received injected data length (%x).\n", rc);
	if (rc <= 0) {
		goto failed;
	}

	assert(rc == data_len);

	/*
	 * Checks the buffer is correct or not.
	 */
	rc = _check_buffer(data, data_len);

	if (rc == 0) {
		printf("Received injected data correct.\n");
	} else {
		printf("Received injected data wrong.\n");
	}

failed:
	if (data) {
		free(data);
		data = NULL;
	}
	return rc;
}
