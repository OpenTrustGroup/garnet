// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#include <nbl_fwk_ree/err.h>
#include <kmsetkey_ipc.h>
#include <nbl_fwk_ree/log.h>
#include <nbl_fwk_ree/common.h>
#include <nbl_fwk_ree/service.h>

#define KMSETKEY_PORT		"com.mediatek.kmsetkey"
#define LOG_TAG "simple_kmsetkey"

#define MAX_PATH_LEN 128

static nbl_chn_t *chn;
static char *srv_name = KMSETKEY_PORT;
const char GOOGLE_FILE_PREFIX[] = "/root/ree/google_data";

int kmsetkey_connect(){
    int rc = NBL_NO_ERROR;
    rc = nbl_chn_open(srv_name,0,&chn);
    if(rc != NBL_NO_ERROR){
        NBL_LOGE("Connect to %s failed.\n", srv_name);
    }

    return 0;
}

int kmsetkey_call(const uint32_t cmd, const bool finish, const uint8_t *in, const uint32_t in_size, uint8_t *out, uint32_t *out_size){
    int rc = NBL_NO_ERROR;

    size_t msg_size = sizeof(struct kmsetkey_msg) + in_size;
    struct kmsetkey_msg *msg = (struct kmsetkey_msg *)malloc(msg_size);
    msg->cmd = cmd;
    if (in_size > 0)
        memcpy(msg->payload, in, in_size);

    rc = nbl_chn_write_buf(chn, msg ,msg_size);
    if(rc != msg_size){
        NBL_LOGE("Service Client write buffer failed (%d)\n", rc);
        return -1;
    }
    free(msg);

    if (rc < 0) {
        NBL_LOGE("failed to send cmd (%d) to %s: %s\n", cmd, KMSETKEY_PORT, strerror(errno));
        return -1;
    }

    rc = nbl_chn_read_buf(chn, out, *out_size, NBL_TIMEOUT_INFINITY);
    if (rc < 0){
        NBL_LOGE("Service Client read buffer failed (%d).\n", rc);
        return -1;
    }

    if ((size_t)rc < sizeof(struct kmsetkey_msg)) {
        NBL_LOGE("invalid response size (%d)\n", (int)rc);
        printf("invalid response size (%d)\n", (int)rc);
        return -1;
    }

    msg = (struct kmsetkey_msg *)out;
    if ((finish && msg->cmd != (cmd | RESP_FLAG | DONE_FLAG)) || (!finish && msg->cmd != (cmd | RESP_FLAG))) {
        NBL_LOGE("invalid response (%u) for cmd (%u)\n", msg->cmd, cmd);
        return -1;
    }

    *out_size = (uint32_t)rc;

    return 0;
}

void kmsetkey_disconnect(){
    nbl_chn_close(chn);
}

static uint32_t kb_offset = 0, kb_len = 0;
static uint8_t *kb_buf = NULL;

static void attest_keybox_clear(void)
{
    kb_offset = 0;
    kb_len = 0;
    free(kb_buf);
    kb_buf = NULL;
}

int32_t ree_import_atteest_keybox(const uint8_t *peakb, const uint32_t peakb_len, const bool finish){
    int32_t rc;
    uint32_t payload_offset, payload_len, out_size;
    uint8_t out[MAX_MSG_SIZE], *tmp_buf;
    struct kmsetkey_msg *msg;

    if (peakb == NULL || peakb_len == 0) {
        NBL_LOGE("attest keybox is NULL\n");
        return -1;
    }

    payload_offset = kb_offset + peakb_len;
    if (payload_offset > kb_len) {
        payload_len = finish ? payload_offset : 2 * payload_offset;
        tmp_buf = (uint8_t *)malloc(payload_len);
        memcpy(tmp_buf, kb_buf, kb_offset);
        free(kb_buf);
        kb_buf = tmp_buf;
        kb_len = payload_len;
    }
    memcpy(kb_buf + kb_offset, peakb, peakb_len);
    kb_offset += peakb_len;

    if (!finish)
        return 0;

    rc = kmsetkey_connect();
    if (rc < 0) {
        NBL_LOGE("kmsetkey_connect failed: %d\n", rc);
        attest_keybox_clear();
        return rc;
    }

    out_size = MAX_MSG_SIZE;
    rc = kmsetkey_call(KEY_LEN, true, (uint8_t *)&kb_offset, sizeof(uint32_t), out, &out_size);
    if (rc < 0) {
        NBL_LOGE("kmsetkey_call failed: %d for cmd %u.\n", rc, KEY_LEN);
        goto exit;
    }
    for (payload_offset = 0; payload_offset < kb_offset; payload_offset += payload_len) {
        payload_len = kb_offset - payload_offset < MAX_MSG_SIZE - IPC_MSG_SIZE - sizeof(struct kmsetkey_msg) ? kb_offset - payload_offset : MAX_MSG_SIZE - IPC_MSG_SIZE - sizeof(struct kmsetkey_msg);
        out_size = MAX_MSG_SIZE;
        rc = kmsetkey_call(KEY_BUF, payload_offset + payload_len < kb_offset ? false : true, kb_buf + payload_offset, payload_len, out, &out_size);
        if (rc < 0) {
            NBL_LOGE("kmsetkey_call failed: %d for cmd %u \n", rc, KEY_BUF);
            goto exit;
        }
    }

    out_size = MAX_MSG_SIZE;
    rc = kmsetkey_call(SET_KEY, true, NULL, 0, out, &out_size);
    if (rc < 0) {
        NBL_LOGE("kmsetkey_call failed: %d for cmd %u\n", rc, SET_KEY);
    }
exit:
    attest_keybox_clear();
    kmsetkey_disconnect();
    return rc < 0 ? rc : 0;

}

struct google_data {
    const char filename[256];
};

static struct google_data google[] = {
        {"kb_0000000000.bin"},
        {"kb_0000000003.bin"}
};

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
        printf("run simple_kmsetkey\n");
	(void)argc; (void)argv;
	
    uint32_t ret = 0;
    int rc = NBL_NO_ERROR;
    FILE *fp;
    void *data = NULL;
    int size = 0;
    char data_path[MAX_PATH_LEN] = "";

    snprintf(data_path,MAX_PATH_LEN - 1,"%s/%s",GOOGLE_FILE_PREFIX,google[0].filename);

    if((fp = fopen(data_path,"r")) == NULL){
        NBL_LOGE("Open file %s failed.\n",data_path);
        return NBL_ERR_NOT_FOUND;
    }

    rc = fseek(fp, 0, SEEK_END);
    if (rc){
        NBL_LOGE("Seek file %s end failed.\n",data_path);
        rc = NBL_ERR_IO;
        goto seek_file_end_failed;
    }

    size = ftell(fp);
    if(size < 0){
        NBL_LOGE("Get file %s length failed, err=%s.\n",data_path,strerror(errno));
        rc = NBL_ERR_IO;
        goto get_length_failed;
    }

    rc = fseek(fp, 0, SEEK_SET);
    if (rc) {
        NBL_LOGE("Seek file %s start failed.\n", data_path);
        rc = NBL_ERR_IO;
        goto seek_file_start_failed;
    }

    data = malloc(size);
    if (!data) {
        NBL_LOGE("Alloc buffer failed (%d).\n", size);
        rc = NBL_ERR_NO_MEMORY;
        goto alloc_buf_failed;
    }

    rc = fread(data,1,size,fp);
    if (rc != size) {
        NBL_LOGE("Read file failed (%d, %d).\n", rc, size);
        rc = NBL_ERR_IO;
        goto read_file_failed;
    }

    fclose(fp);

    ret = ree_import_atteest_keybox((uint8_t *)data, size, true);
    
    if (ret == 0) {
        printf("run simple_kmsetkey success\n");
    } else {
        printf("run simple_kmsetkey failed %d\n", ret);
    }
    return ret;


read_file_failed:
    free(data);
alloc_buf_failed:
get_length_failed:
seek_file_start_failed:
seek_file_end_failed:
    fclose(fp);
    return NBL_ERR_NOT_VALID;
}
