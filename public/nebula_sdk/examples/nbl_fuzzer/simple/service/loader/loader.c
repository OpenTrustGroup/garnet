// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <service_utils.h>

#define LOADER_PORT_NAME "com.grt.loader.srv.main"

enum {
  GRT_LOADER_SEND_FILE_DATA = 1,
  GRT_LOADER_SEND_FILE_NAME = 2,
  GRT_LOADER_REE_TA_REQ = 3,
  GRT_LOADER_SEND_ABORT = 4,
  GRT_LOADER_SEND_RESPONSE = 5,
  GRT_LOADER_LOAD_TA = 6,
  GRT_LOADER_LOAD_SECMGR_TA = 7,
  // On end of TA run, return info to callee.
  GRT_LOADER_TA_RUN_END = 8,

  GRT_LOADER_LOAD_CHANNEL_TEST = 99,
  GRT_LOADER_START_LEAK_DETECT = 100,
  GRT_LOADER_STOP_LEAK_DETECT = 101
};

static int LOADER_CMDS[] = {GRT_LOADER_SEND_RESPONSE, GRT_LOADER_LOAD_SECMGR_TA,
                            GRT_LOADER_TA_RUN_END,
                            GRT_LOADER_LOAD_CHANNEL_TEST};
static uuid_t LOADER_UUIDS[] = {
    {0x94e8bd48,
     0xbc3f,
     0x4461,
     {0xa4, 0x13, 0x59, 0x93, 0x72, 0x4d, 0x17, 0xf0}},
    {0xfbed7a6a,
     0x2b57,
     0x4b3c,
     {0xa9, 0xc2, 0x75, 0xe9, 0x92, 0xc3, 0x60, 0x3d}},
    {0xab742471,
     0xd6e6,
     0x4806,
     {0x85, 0xf6, 0x05, 0x55, 0xb0, 0x24, 0xf4, 0xda}},
    {0xabbbbbb,
     0xd6e6,
     0x4806,
     {0x85, 0xf6, 0x05, 0x55, 0xb0, 0x24, 0xf4, 0xda}}};

static void fill_loader_cmd(loader_cmt_t *cmd) {
  int res;
  uint32_t uuid_inx;
  uint32_t cmd_inx;

  res = fuz_draw_random(cmd, sizeof(*cmd));
  if (res != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, res);
    exit(-1);
  }

  cmd->param_size = sizeof(uuid_info_t);
  if (!ONE_IN(5)) {
    uuid_inx = fuz_rnd() % 4;
    memcpy(&(cmd->param), &(LOADER_UUIDS[uuid_inx]), sizeof(uuid_t));
    cmd_inx = fuz_rnd() % 4;
    cmd->cmd = LOADER_CMDS[cmd_inx];
    fuz_print_log("cmd=%d, uid_inx=%d, ", cmd->cmd, uuid_inx);
  }
}

static void test_loader(void *arg) {
  (void)arg;

  if (ONE_IN(10)) {
    send_random_msg(LOADER_PORT_NAME);
  } else {
    loader_cmt_t *cmd = malloc(sizeof(loader_cmt_t));
    if (cmd == NULL) {
      fuz_print_log("Error: %s %d\n", __func__, __LINE__);
      exit(-1);
    } else {
      register_fuz_mem(cmd);
    }

    fill_loader_cmd(cmd);
    send_msg_to_service(LOADER_PORT_NAME, cmd, sizeof(loader_cmt_t), false, 0);
  }
}

struct function_description load_app_by_uuid_test = {.do_custom_test =
                                                         test_loader};

PrepareTest(load_app_by_uuid) { return &load_app_by_uuid_test; }
