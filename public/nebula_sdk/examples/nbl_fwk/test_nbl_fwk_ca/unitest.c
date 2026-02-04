// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * This example will invoke the HA by loader API.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "trusty/tipc.h"
#include "uapi/err.h"

#include <nbl_fwk_ree/log.h>
#include <nbl_fwk_ree/service.h>
#include <nbl_fwk_ree/stream.h>

#include "loader.h"

#define LOG_TAG "libnbl_fwk_ree-unittest"
#define true 1
#define false 0

#define PAGE_SIZE 4096

#define TIPC_DEFAULT_DEVNAME "/dev/trusty-ipc-dev0"
const nbl_uuid_t NBL_SVR_UUID = {
    0x1250c17f,
    0x7ebf,
    0x42aa,
    {0x91, 0xa3, 0xfd, 0xe5, 0x1b, 0xdb, 0xcc, 0x02}};
const nbl_uuid_t NBL_OTHER_SVR_UUID = {
    0x093917a8,
    0x8583,
    0x4d8f,
    {0x94, 0x2f, 0xf7, 0x6e, 0xb2, 0x1e, 0x34, 0x57}};

static const char* test_service_name = "com.nebula.grt.trusty-fw.test";

static void send_command(int cmd_id) {
  int rc, rc_hee = -1;
  nbl_chn_t* chn;
  nbl_stm_t* cmd_request;
  nbl_stm_t* cmd_response;

  rc = nbl_load_app_by_uuid(0, &NBL_SVR_UUID, NULL);
  if (rc != NO_ERROR) {
    fprintf(stderr, "Cannot start hee.\n");
    return;
  }

  rc = nbl_load_app_by_uuid(0, &NBL_OTHER_SVR_UUID, NULL);
  if (rc != NO_ERROR) {
    fprintf(stderr, "Cannot start hee_other.\n");
    return;
  }

  rc = nbl_chn_open(test_service_name, 0, &chn);
  if (rc < 0) {
    fprintf(stderr, "Failed to connect to service\n");
    return;
  }

  cmd_request = nbl_stm_create();
  nbl_stm_add_int(cmd_request, cmd_id);

  cmd_response = nbl_stm_create();

  rc = nbl_chn_write_stm_sync(chn, cmd_request, cmd_response,
                              NBL_TIMEOUT_INFINITY);
  if (rc != NO_ERROR) {
    fprintf(stderr, "Failed to write or read info of HA.\n");
    goto release;
  }

  rc = nbl_stm_next_int(cmd_response, &rc_hee);
  if (rc != NO_ERROR) {
    fprintf(stderr, "Failed to read ressult from HA.\n");
    goto release;
  }

  if (rc_hee == NO_ERROR) {
    fprintf(stdout, "Grt fwk test in SDK is OK.\n");
  } else {
    fprintf(stderr, "Grt fwk test in SDK is FAILED.\n");
  }

release:
  nbl_stm_destroy(cmd_request);
  nbl_stm_destroy(cmd_response);
  nbl_chn_close(chn);
}

int main(int argc, char** argv) {
  send_command(4);
}
