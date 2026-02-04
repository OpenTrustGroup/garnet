// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <service_utils.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <nbl_attest/attest_api.h>

#define ATTEST_PORT "com.grt.attest.srv.main"

#define ATS_MSG_MAJOR_VERSION 0u
#define ATS_MSG_MINOR_VERSION 1u

#define ATS_MESSAGE_VERSION                                                    \
  ((ATS_MSG_MAJOR_VERSION << 16) | ATS_MSG_MINOR_VERSION)

#define ATS_REQ_SHIFT 3

#define ATS_OFFLINE_ATTEST (1 << ATS_REQ_SHIFT)
#define ATS_ONLINE_ATTEST (2 << ATS_REQ_SHIFT)
#define ATS_ONLINE_ATTEST2 (3 << ATS_REQ_SHIFT)
#define ATS_NFC_CONNECT (4 << ATS_REQ_SHIFT)
#define ATS_NFC_TRANSCEIVE (5 << ATS_REQ_SHIFT)
#define ATS_NFC_DISCONNECT (6 << ATS_REQ_SHIFT)
#define ATS_BUFFER_TEST (100 << ATS_REQ_SHIFT)

#define NFC_MAX_DATA_LEN 256

typedef struct cmd_buf {
  uint32_t cmd;
  uint8_t *in;
  uint32_t in_bytes;
  uint8_t *out;
  uint32_t out_bytes;
  uint32_t timeout;
} cmd_buf_t;

typedef struct __attribute__((packed)) {
  uint32_t tech;
  uint32_t timeout;
  char data[NFC_MAX_DATA_LEN];
} ats_nfc_connect_req_t;

int invoke_cmd(cmd_buf_t *cmd_buf);
static void send_attest_msg(uint32_t cmd, void *buf, uint32_t size) {
  cmd_buf_t cmd_buf;
  void *out_buf;

  cmd_buf.cmd = cmd;
  cmd_buf.in = buf;
  cmd_buf.in_bytes = size;
  out_buf = malloc(4096);
  if (out_buf == NULL) {
    cmd_buf.out_bytes = 0;
  } else {
    cmd_buf.out = out_buf;
    cmd_buf.out_bytes = 4096;
    register_fuz_mem(out_buf);
  }
  invoke_cmd(&cmd_buf);
}

static uint32_t ATTEST_CMDS[] = {ATS_OFFLINE_ATTEST, ATS_ONLINE_ATTEST,
                                 ATS_ONLINE_ATTEST2, ATS_NFC_CONNECT,
                                 ATS_NFC_TRANSCEIVE, ATS_NFC_DISCONNECT};
static void test_attest_random(uint32_t cmd, void *buf, uint32_t size) {
  (void)cmd;
  fuz_draw_random(buf, size);
  send_attest_msg(fuz_rnd(), buf, size);
}
struct function_description attest_random_test = {
    .funcname = "attest_random",
    .funcaddr = test_attest_random,
    .argnum = 3,
    .argname = {"cmd", "buf", "size"},
    .argtype = {ARG_SELECT, ARG_NORMAL_BUF, ARG_NORMAL_BUF_SIZE},
    .argselcnt[0] = ARRAY_SIZE(ATTEST_CMDS),
    .argselect[0] = ATTEST_CMDS,
    .argmax[1] = 3072,
    .argdep[2] = 1 + ARG_DEPEND_MIN};
PrepareTest(attest_random) { return &attest_random_test; }

static uint32_t NFC_TECHS[] = {
    0,  // NFC_TECH_ISO_DEP
    1,  // NFC_TECH_NFC_A
    2,  // NFC_TECH_NFC_B
    3,  // NFC_TECH_NFC_V
    100 // NFC_TECH_MOCK
};
void test_nfc_connect(void *buf, uint32_t size, uint32_t tech) {
  if (ONE_IN(10)) {
    fuz_draw_random(buf, size);
    send_attest_msg(ATS_NFC_CONNECT, buf, size);
    return;
  }
  if (size >= 8) {
    memset(buf, 0, size);
    ats_nfc_connect_req_t *req = (ats_nfc_connect_req_t *)buf;
    req->tech = tech;
    req->timeout = 12;
    if (!ONE_IN(10)) {
      uint32_t *pointer = buf;
      fuz_draw_random(pointer + 2, size - 8);
    }
  }
}
struct function_description attest_nfc_connect_test = {
    .funcname = "attest_nfc_connect",
    .funcaddr = test_nfc_connect,
    .argnum = 3,
    .argname = {"buf", "size", "tech"},
    .argtype = {ARG_NORMAL_BUF, ARG_NORMAL_BUF_SIZE, ARG_SELECT},
    .argmax[0] = 3072,
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argselcnt[2] = ARRAY_SIZE(NFC_TECHS),
    .argselect[2] = NFC_TECHS};
PrepareTest(attest_nfc_connect) { return &attest_nfc_connect_test; }

static void test_attest_offline() {
  attest_req_t req;
  fuz_draw_random(&req, sizeof(req));
  send_attest_msg(ATS_OFFLINE_ATTEST, &req, sizeof(req));
}
struct function_description attest_offline_test = {
    .funcname = "attest_offline",
    .funcaddr = test_attest_offline,
    .argnum = 0,
};
PrepareTest(attest_offline) { return &attest_offline_test; }

typedef struct __attribute__((packed)) {
  uint32_t size_in_bytes;
  uint32_t init_value;
  uint32_t step_value;
  uint32_t from_nsc;
} ats_buffer_test_req_t;

static void test_attest_buffer(uint32_t size_in_bytes, uint32_t init_value,
                               uint32_t step_value) {
  ats_buffer_test_req_t req;

  req.size_in_bytes = size_in_bytes;
  req.init_value = init_value;
  req.step_value = step_value;
  req.from_nsc = RAND_BOOL() ? 0 : 1;
  send_attest_msg(ATS_BUFFER_TEST, &req, sizeof(req));
}
struct function_description attest_buffer_test = {
    .funcname = "attest_buffer",
    .funcaddr = test_attest_buffer,
    .argnum = 3,
    .argtype = {ARG_INT, ARG_INT, ARG_INT},
    .argmax = {4096, 4096, 4096}};
PrepareTest(attest_buffer) { return &attest_buffer_test; }
