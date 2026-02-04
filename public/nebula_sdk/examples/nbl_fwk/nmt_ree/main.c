// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nbl_fwk_ree/err.h>
#include <nbl_fwk_ree/nmt_ree.h>

static int injectData(const char *uuid, int slot, void *data, int size) {
	int rc = NBL_NO_ERROR;
	uint32_t dest_off = 0;
	uint32_t inject_size = 0;

	do {
		inject_size =
				size > NMT_MAX_TRANSFER_DATA_SIZE ?
						NMT_MAX_TRANSFER_DATA_SIZE : size;

		rc = nbl_plt_data_inject(uuid, slot, NMT_INJECT_FLAG_TDEA,
				(uint8_t *) data + dest_off, inject_size, dest_off);
		if (rc != NBL_NO_ERROR) {
			printf("Inject data failed (%d).\n", rc);
			break;
		}

		dest_off += inject_size;
		size -= inject_size;
	} while(size > 0);

	return rc;
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	int rc = -1;
	int ta_slot = 1;
	int data_len = 16 * 1024;
	char *data = (char *)malloc(data_len);
	int i = 0;

	/* The TA is in examples/nbl_fwk/nmt_tee. The UUID is defined in
	 * examples/nbl_fwk/nmt_tee/manifest.c.
	 */
	char *ta_uuid = "4591c6d6-01cb-11ea-aa11-5770f01cffaa";

	for (i = 0; i < data_len; i++) {
		data[i] = i % 256;
	}

	rc = injectData(ta_uuid, ta_slot, data, data_len);

	free(data);

	return rc;
}
