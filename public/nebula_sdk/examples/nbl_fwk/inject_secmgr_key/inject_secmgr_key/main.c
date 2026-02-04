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

int readFile(const char* filename, char* buffer, int bufferSize) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        printf("open file:%s failed！\n", filename);
        return -1;
    } else {
        printf("open file:%s\n", filename);
    }
 
    int bytesRead = fread(buffer, sizeof(char), bufferSize, file);
    fclose(file);
 
    return bytesRead;
}

int main(int argc, char* argv[]) {
        printf("run inject_secmgr_key demo\n");
	(void)argc; (void)argv;
	int rc = -1;
	int data_len = 4096;
	char *data = (char *)malloc(data_len);
	int i = 0;

	char *ta_uuid = "2634b092-c157-11e8-932f-db31082a06f5";

        //QEMU read key and cert
        //Need to execute "push_key_to_qemu.sh" pushes new_test_key/export_key to QEMU "root/ree/secmgr_test_key"
        memset(data, 0, data_len);
        int readCount = readFile("/root/ree/secmgr_test_key/hee_ca_cert", data, data_len);
        printf("read file data size %d\n",readCount);
        rc = injectData(ta_uuid, 1, data, readCount);
        if (rc != 0) {
            printf("Inject hee_ca_cert failed (%d).\n", rc);
        }
        
        memset(data, 0, data_len);
        readCount = readFile("/root/ree/secmgr_test_key/hee_cert", data, data_len);
        printf("read file data size %d\n",readCount);
        rc = injectData(ta_uuid, 2, data, readCount);
        if (rc != 0) {
            printf("Inject hee_cert failed (%d).\n", rc);
        }
        
        memset(data, 0, data_len);
        readCount = readFile("/root/ree/secmgr_test_key/hee_private_key", data, data_len);
        printf("read file data size %d\n",readCount);
        rc = injectData(ta_uuid, 3, data, readCount);
        if (rc != 0) {
            printf("Inject hee_private_key failed (%d).\n", rc);
        }
        
        memset(data, 0, data_len);
        readCount = readFile("/root/ree/secmgr_test_key/spaik_provision", data, data_len);
        printf("read file data size %d\n",readCount);
        rc = injectData(ta_uuid, 6, data, readCount);
        if (rc != 0) {
            printf("Inject spaik_provision failed (%d).\n", rc);
        }
        
        memset(data, 0, data_len);
        readCount = readFile("/root/ree/secmgr_test_key/tsm_root_cert", data, data_len);
        printf("read file data size %d\n",readCount);
	rc = injectData(ta_uuid, 10, data, readCount);
	if (rc != 0) {
            printf("Inject tsm_root_cert failed (%d).\n", rc);
        }

	free(data);
        printf("Inject inject_secmgr_key finish\n");
	return 0;
}
