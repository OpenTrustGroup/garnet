// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Normal Service using shared memory.
 * 1. 1M shared memory is split to two region with 512KB.
 * 2. Service shall only write and read 256KB in first region.
 * 	It's used to make first region be loaded into cache.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nbl_fwk/service.h>

#include "../common.h"

#define LOG_TAG "cache service"

typedef struct session_a {
  int value;
  int data_size;
  void* buf;
  void* shm_base;
  nbl_shm_t* shm;
  nbl_stm_t* ack;
} session_a_t;

nbl_app_t* app = NULL;

static int _srv_a_connect_handler(nbl_srv_t* srv, nbl_uuid_t peer_uuid) {
  NBL_LOGI("Connect.\n");
  return NBL_NO_ERROR;
}

static void _srv_a_open_handler(nbl_chn_t* chn) {
  session_a_t* session = calloc(1, sizeof(session_a_t));
  session->buf = malloc(256);
  session->ack = nbl_stm_create();
  nbl_chn_set_priv(chn, session);
  NBL_LOGI("handler invoked.\n");
}

// Access data in first 256KB
static void use_data(char* buf, size_t len) {
  typedef unsigned long TYPE;
  TYPE* p = NULL;
  for (int i = 0; i < STEP_COUNT; i++) {
    if (i % 2 == 0) {
      p = (TYPE*)(buf + i * ACCESS_STEP);
      for (int j = 0; j < 256 * 1024 / sizeof(TYPE); j++) {
        for (int k = 0; k < 100; k++) {
          p[j] += i * j * k;
        }
      }
    }
  }
}

static int _srv_a_stm_handler(nbl_chn_t* chn, nbl_stm_t* stm) {
  int rc;
  int cmd;
  session_a_t* session = nbl_chn_get_priv(chn);

  rc = nbl_stm_next_int(stm, &cmd);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Service A Get cmd failed (%d).\n", rc);
    goto get_cmd_failed;
  }

  switch (cmd) {
    case CMD_MAP_SHM:
      nbl_stm_next_shm(stm, &session->shm);
      nbl_shm_map(session->shm, NBL_SHM_PERM_RW, &session->shm_base);
      break;
    case CMD_SEND_MSG:
      NBL_LOGI("Share Memory:%s\n", session->shm_base);
      break;
    case CMD_NOTIFY_DATA: {
      use_data(session->shm_base, SHARED_MEM_SIZE);
      // NBL_LOGI("Use shared memory\n");
      break;
    }
    case CMD_UNMAP_SHM:
      nbl_shm_unmap(session->shm);
      nbl_shm_free(session->shm);
      session->shm = NULL;

      nbl_app_stop(app);
      break;
  }

  nbl_stm_add_int(session->ack, cmd);
  nbl_chn_write_stm(chn, session->ack);

  return NBL_NO_ERROR;

get_cmd_failed:
  return rc;
}

static void _srv_a_close_handler(nbl_chn_t* chn) {
  session_a_t* session = nbl_chn_get_priv(chn);
  if (session) {
    if (session->buf) {
      free(session->buf);
    }

    if (session->shm) {
      nbl_shm_free(session->shm);
      session->shm = NULL;
      session->shm_base = NULL;
    }

    if (session->ack) {
      nbl_stm_destroy(session->ack);
      session->ack = NULL;
    }

    free(session);
  }
  NBL_LOGI("Service closes handler.\n");
}

static nbl_srv_desc_t _services[] = {
    {.name = SRV_NAME,
     .flags = NBL_SRV_FLAG_SCOPE_REE,
     .buf_num = NBL_SRV_DEFAULT_BUF_NUM,
     .buf_size = NBL_SRV_MAX_BUF_SIZE,
     .conn_handler = _srv_a_connect_handler,
     .open_handler = _srv_a_open_handler,
     .stm_handler = _srv_a_stm_handler,
     .buf_handler = NULL,
     .close_handler = _srv_a_close_handler},
};

/**
 * Start a service and wait for TA1 to shared memory.
 * And then use shared memory.
 */
int main() {
  int rc = NBL_NO_ERROR;
  nbl_srv_t* srv = NULL;
  app = nbl_app_create();
  unsigned i;

  NBL_LOGI("App service starts.\n");

  for (i = 0; i < sizeof(_services) / sizeof(nbl_srv_desc_t); i++) {
    rc = nbl_srv_create(&_services[i], NULL);
    if (rc != NBL_NO_ERROR) {
      NBL_LOGE("Create service %s failed!", _services[i].name);
      goto create_service_test_failed;
    }

    rc = nbl_app_reg_srv(app, _services[i].srv);
    if (rc != NBL_NO_ERROR) {
      NBL_LOGE("Register service %s failed!", _services[i].name);
      goto reg_service_test_failed;
    }
  }

  nbl_app_main_loop(app);

reg_service_test_failed:
create_service_test_failed:
  for (i = 0; i < sizeof(_services) / sizeof(nbl_srv_desc_t); i++) {
    nbl_srv_destroy(_services[i].srv);
  }
  nbl_app_destroy(app);
  app = NULL;
  return rc;
}
