// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
/**
 * Test API of nbl_fwk on SERVER.
 * Client API will not have SECURE problem, so don't test.
 */

#include <fuz_common.h>

#include <pthread.h>

#include <trusty_std.h>
#include <uapi/err.h>
#include <zircon/nslist.h>

#include <service_utils.h>

#include <nbl_fwk/service.h>

#include "service/nsc/nsc_utils.h"

/**
 * Test Method:
 * 1. Create a client and server, client send fuzzing message to server.
 *    Server receives this message, and parse it.
 * 2. Flow:
 *    Server in this process creates a port, waits sent message.
 *    Client in this process too, send fuzzing message to server.
 *    Server shall receive and parse message, not crash, return OK and ERROR.
 */

// from /nebula/third_party/hee-trusty/nbl_fwk/src/_stream.h
typedef struct nbl_stm_header {
  uint32_t magic;
  uint16_t version;
  uint16_t handle_table;
  uint32_t reserves[2];
} nbl_stm_header_t;

typedef struct nbl_fwk_cmd_param_data {
  uint32_t data_size;
} nbl_fwk_cmd_param_data_t;

#define NBL_STM_HEADER_MAGIC 0x3d61e8e7
#define NBL_STM_VERSION 1

////////////////////////////////////////////////////////////
// Test cases.

////////////////////////////////////////////////////////////
// Send fuzzing message to server for parsing.

#define FUZ_FWK_SRV_PORT_NAME "com.grt.fuz.fwk.srv"

// record sequence of adding elements to stream.
// Type of item is "added_t".
nslist_t g_added_list = LIST_INITIAL_VALUE;

typedef enum {
  ADD_INT = 0,
  ADD_BUF = 1,
} added_type_t;

typedef struct {
  added_type_t type;
  size_t len;
} added_t;

// this mutex is like signal.
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int _fuz_fwk_srv_stm(nbl_chn_t *chn, nbl_stm_t *stm) {
  (void)chn;
  int rc;
  int count;
  nbl_stm_t *resp = nbl_stm_create();

  // Fuzzing read
  if (nslist_is_empty(&g_added_list) || ONE_IN(10)) {
    count = 100;
    do {
      switch (fuz_rnd() % 3) {
      case 0: {
        int value;
        rc = nbl_stm_next_int(stm, &value);
        break;
      }
      case 1: {
        int out_size;
        char buf[4096]; // MAX MEMORY.
        rc = nbl_stm_next_buf(stm, &out_size, buf);
        break;
      }
      case 2: {
        int out_size;
        void *out_data_ptr;
        rc = nbl_stm_next_buf_ptr(stm, &out_size, &out_data_ptr);
        break;
      }
      }
      count--;
    } while (count > 0);
  } else {
    // read data by added sequence.
    nslist_t *next;
    for (next = g_added_list.next; next != NULL; next = next->next) {
      added_t *added = next->data;

      switch (added->type) {
      case ADD_INT: {
        int value;
        rc = nbl_stm_next_int(stm, &value);
        break;
      }
      case ADD_BUF: {
        if (ONE_IN(2)) {
          int out_size;
          char buf[4096]; // MAX MEMORY.
          rc = nbl_stm_next_buf(stm, &out_size, buf);
        } else {
          int out_size;
          void *out_data_ptr;
          rc = nbl_stm_next_buf_ptr(stm, &out_size, &out_data_ptr);
        }
        break;
      }
      default:
        fuz_print_log("Error: %s:%d, meet unknown added type=%d\n", __func__,
                      __LINE__, added->type);
        exit(1);
      }
    }
  }

  nbl_stm_destroy(resp);

  return NBL_NO_ERROR;
}

static nbl_srv_desc_t fuz_fwk_services[] = {
    {
        .name = FUZ_FWK_SRV_PORT_NAME,
        .flags = NBL_SRV_FLAG_SCOPE_HEE,
        .buf_num = NBL_SRV_DEFAULT_BUF_NUM,
        .buf_size = NBL_SRV_MAX_BUF_SIZE,
        .conn_handler = NULL,
        .open_handler = NULL,
        .stm_handler = _fuz_fwk_srv_stm,
        .buf_handler = NULL,
        .close_handler = NULL,
    },
};

/**
 * Run in server thread.
 * Parse received message.
 */
static void *thread_entry_server(void *arg) {
  (void)arg;
  int rc;
  nbl_app_t *s_app;

  s_app = nbl_app_create();

  for (size_t i = 0; i < sizeof(fuz_fwk_services) / sizeof(nbl_srv_desc_t);
       i++) {
    rc = nbl_srv_create(&fuz_fwk_services[i], NULL);
    if (rc != NO_ERROR) {
      fuz_print_log("Error: %s %d, Create service %s failed!\n", __func__,
                    __LINE__, fuz_fwk_services[i].name);
      goto create_service_failed;
    }

    rc = nbl_app_reg_srv(s_app, fuz_fwk_services[i].srv);
    if (rc != NO_ERROR) {
      fuz_print_log("Error: %s %d, Register service %s failed!\n", __func__,
                    __LINE__, fuz_fwk_services[i].name);
      goto reg_service_failed;
    }
  }

  pthread_mutex_unlock(&mutex);

  // Only on exit, the function will return.
  nbl_app_main_loop(s_app);

reg_service_failed:
create_service_failed:
  for (size_t i = 0; i < sizeof(fuz_fwk_services) / sizeof(nbl_srv_desc_t);
       i++) {
    nbl_srv_destroy(fuz_fwk_services[i].srv);
  }
  nbl_app_destroy(s_app);

  return NULL;
}

// Create and fill a buffer, with VALID/INVALID message content with any length.
void *fuz_fill_fwk_msg(size_t *buf_size) {
  size_t size;

  nslist_clear(&g_added_list, free);
  nslist_init(&g_added_list);

  if (ONE_IN(20)) {
    // TOO LARGE MEMORY cannot be transferred to server.
    size = fuz_rnd() % MAX_BUF_SIZE;
  } else {
    size = fuz_rnd() % 4096;
  }

  void *buf = malloc(size);
  if (buf == NULL) {
    exit(1);
  } else {
    register_fuz_mem(buf);
  }

  if (ONE_IN(20) || size < sizeof(nbl_stm_header_t) + sizeof(uint32_t)) {
    // fill any data.
    draw_data_by_trusty_prng(size, buf);
  } else {
    // fill header with normal or anormal data.
    nbl_stm_header_t *header = (nbl_stm_header_t *)buf;
    header->magic = ONE_IN(20) ? fuz_rnd() : NBL_STM_HEADER_MAGIC;
    header->version = ONE_IN(20) ? fuz_rnd() : NBL_STM_VERSION;
    header->handle_table = fuz_rnd();
    header->reserves[0] = fuz_rnd();
    header->reserves[1] = fuz_rnd();

    void *data = buf + sizeof(nbl_stm_header_t);
    size_t data_len = size - sizeof(nbl_stm_header_t);
    size_t one_len = 0;
    if (ONE_IN(10)) {
      // fill data with random data.
      draw_data_by_trusty_prng(data_len, data);
    } else {
      // fill data with right int or buf.
      int try_count = 5;
      do {
        switch (fuz_rnd() % 2) {
        case 0: { // Add int
          one_len = sizeof(int);
          if (data_len < one_len) {
            try_count--;
            one_len = 0;
            break;
          }
          *((int *)data) = fuz_rnd();

          added_t *added = (added_t *)calloc(1, sizeof(added_t));
          added->type = ADD_INT;
          nslist_add_tail(&g_added_list, added);
          break;
        }
        case 1: { // Add buf = Add buf ptr
          one_len = fuz_rnd() % data_len;
          size_t head_len = sizeof(nbl_fwk_cmd_param_data_t);
          if (one_len <= head_len || data_len < one_len) {
            try_count--;
            one_len = 0;
            break;
          }
          nbl_fwk_cmd_param_data_t *param = (nbl_fwk_cmd_param_data_t *)data;
          param->data_size = one_len - head_len;
          draw_data_by_trusty_prng(one_len - head_len, data + head_len);

          added_t *added = (added_t *)calloc(1, sizeof(added_t));
          added->type = ADD_BUF;
          added->len = param->data_size;
          nslist_add_tail(&g_added_list, added);

          break;
        }
        } // switch
        data += one_len;
        data_len -= one_len;
      } while (data_len > 0 && try_count >= 0);
    }
  }

  *buf_size = size;
  return buf;
}

/**
 * Customized Test Function.
 * 1. create thread "server" to start a port, and wait for receiving message.
 * 2. Main thread to connect the port, and send fuzzing message.
 * 3. Thread "server" received message and parse it.
 */
static void test_client_server(void *arg) {
  (void)arg;

  // create a thread and start it.
  pthread_t thrd;
  int res;

  pthread_mutex_lock(&mutex);

  res = pthread_create(&thrd, NULL, thread_entry_server, NULL);
  if (res != 0) {
    fuz_print_log("%s:%d failed creating thread!\n", __func__, __LINE__);
    return;
  }

  // wait for creating port of server.
  pthread_mutex_lock(&mutex);

  // send fuzzing message to server.
  size_t buf_size;
  void *buf = fuz_fill_fwk_msg(&buf_size);

  res = send_msg_to_service(FUZ_FWK_SRV_PORT_NAME, buf, buf_size, false, 0);
  // If message is error, above function returns OK as right, but channel is
  // closed. So whether or not it's OK, cancel "server" thread. If want clean
  // resources in "server" thread, set pthread_cleanup_push.
  pthread_cancel(thrd);
}

struct function_description fwk_client_server_test = {.do_custom_test =
                                                          test_client_server};

PrepareTest(fwk_client_server) { return &fwk_client_server_test; }
