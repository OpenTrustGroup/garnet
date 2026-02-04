// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <nbl_fwk/service.h>
#include <trusty_std.h>
#include <uapi/err.h>

#include <fuz_common.h>
#include <main/fuz_tester.h>
#include <service_utils.h>

/* App UUID:  {ab742471-d6e6-4806-85f6-0555b024f4da} */
static uuid_t fuzzing_uuid = {0xab742471,
                              0xd6e6,
                              0x4806,
                              {0x85, 0xf6, 0x05, 0x55, 0xb0, 0x24, 0xf4, 0xda}};

typedef struct Data {
  uint32_t cmd;
  int rc;
} Data;

int fuz_start_cmd(const char *cmd_arg) {
  int rc;
  Data data;
  nbl_chn_t *channel;
  loader_cmt_t cmd;
  uuid_info_t *info;

  rc = nbl_chn_open("com.grt.loader.srv.main", -1, &channel);
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = 7; // Load ta from secmgr.
  cmd.param_size = sizeof(uuid_info_t);
  info = &cmd.param;
  memcpy(&info->uuid, &fuzzing_uuid, sizeof(info->uuid));

  snprintf(info->arg, ARG_MAX_LEN, "%s", cmd_arg);

  rc = nbl_chn_write_buf(channel, &cmd, sizeof(cmd));
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  rc = nbl_chn_read_buf(channel, &data, sizeof(data), NBL_TIMEOUT_INFINITY);
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  if (data.cmd != 5) {
    fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
    exit(-1);
  }

  if (data.rc != NBL_NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, data.rc);
    exit(-1);
  }

  rc = nbl_chn_read_buf(channel, &data, sizeof(data), NBL_TIMEOUT_INFINITY);
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }
  if (data.cmd != 8) {
    fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
    exit(-1);
  }

  nbl_chn_close(channel);

  return data.rc;
}

int fuz_start_fuzzing(fuz_seed_t seed, uint32_t no, uint32_t function_inx,
                      uint32_t run_count) {
  char cmd_args[ARG_MAX_LEN + 1];
  snprintf(cmd_args, ARG_MAX_LEN, "-f %u -r 0x%lx -c %u -n %d", function_inx,
           seed, run_count, no);

  return fuz_start_cmd(cmd_args);
}
