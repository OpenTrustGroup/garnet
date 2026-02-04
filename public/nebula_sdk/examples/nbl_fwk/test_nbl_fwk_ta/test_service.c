// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <nbl_fwk/common.h>
#include <nbl_fwk/service.h>
#include <nbl_fwk/stream.h>
#include <trusty_std.h>
#include <uapi/err.h>

#define LOG_TAG "grt-trusty-fwk-test"
#define SRV_NAME(name) "com.nebula.grt.trusty-fw." name
#define OTHER_PORT_NAME SRV_NAME("other_test")

static int test_chan_handler(nbl_chn_t* ctx, nbl_stm_t* cmd);
static nbl_app_t* s_app;
static nbl_srv_desc_t s_services[] = {{.name = SRV_NAME("test"),
                                       .flags = NBL_SRV_FLAG_SCOPE_REE,
                                       .buf_num = NBL_SRV_DEFAULT_BUF_NUM,
                                       .buf_size = NBL_SRV_MAX_BUF_SIZE,
                                       .conn_handler = NULL,
                                       .open_handler = NULL,
                                       .stm_handler = test_chan_handler,
                                       .buf_handler = NULL,
                                       .close_handler = NULL}};

int main() {
  int rc = NO_ERROR;
  nbl_srv_t* srv = NULL;

  s_app = nbl_app_create();

  printf("test_service in SDK starts.\n");

  rc = nbl_srv_create(&s_services[0], &srv);
  if (rc != NO_ERROR) {
    NBL_LOGE("Create service %s failed (%d)\n", SRV_NAME("test"), rc);
    goto create_service_failed;
  }

  rc = nbl_app_reg_srv(s_app, srv);
  if (rc != NO_ERROR) {
    goto reg_service_failed;
  }

  nbl_app_main_loop(s_app);

reg_service_failed:
  nbl_srv_destroy(srv);
create_service_failed:
  nbl_app_destroy(s_app);
  s_app = NULL;
  return NO_ERROR;
}

static void send_response(nbl_chn_t* ctx, int rc) {
  nbl_stm_t* resp = nbl_stm_create();
  nbl_stm_add_int(resp, rc);

  nbl_chn_write_stm(ctx, resp);

  nbl_stm_destroy(resp);
}

static void do_sync_conn_send_test(nbl_chn_t* ctx, nbl_stm_t* cmd) {
  int rc;

  int rc_remote = -1;
  nbl_chn_t* channel = NULL;
  nbl_stm_t* cmd_request;
  nbl_stm_t* cmd_response;

  rc = nbl_chn_open(OTHER_PORT_NAME, -1, &channel);
  if (rc != NO_ERROR) {
    goto connect_error;
  }

  cmd_request = nbl_stm_create();
  nbl_stm_add_int(cmd_request, 1);

  cmd_response = nbl_stm_create();

  rc = nbl_chn_write_stm_sync(channel, cmd_request, cmd_response, -1);
  if (rc < NO_ERROR) {
    rc = ERR_GENERIC;
    goto send_error;
  }

  rc = nbl_stm_next_int(cmd_response, &rc_remote);
  if (rc != NO_ERROR) {
    goto send_error;
  }
  rc = rc_remote;

send_error:
  nbl_stm_destroy(cmd_request);
  nbl_stm_destroy(cmd_response);

connect_error:
  send_response(ctx, rc);
}

static int test_chan_handler(nbl_chn_t* ctx, nbl_stm_t* request) {
  int rc;
  int cmd_id;

  nbl_stm_next_int(request, &cmd_id);

  if (cmd_id != 4) {
    rc = ERR_NOT_FOUND;
    send_response(ctx, rc);
    return NO_ERROR;
  }

  do_sync_conn_send_test(ctx, request);
  return NO_ERROR;
}
