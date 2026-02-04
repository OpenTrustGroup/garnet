// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <nbl_fwk/nmt_tee.h>

#include <fuz_common.h>

#include <service_utils.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <nbl_fwk/service.h>

#define LICENSE_NOTIFY_PORT "com.grt.license.srv.notify"
#define LICENSE_NMT_TEE_PORT "com.grt.license.srv.nmt_tee"

enum {
  NMT_TEE_CMD_NOP = 0,
  NMT_TEE_CMD_LOAD_INJECT_DATA,
};

static void test_license_notify_port() { send_random_msg(LICENSE_NOTIFY_PORT); }
struct function_description license_notify_port_test = {
    .funcname = "license_notify_port",
    .funcaddr = test_license_notify_port,
    .argnum = 0};
PrepareTest(license_notify_port) { return &license_notify_port_test; }

typedef struct nbl_shm {
  void *buf;
  size_t buf_size;
  size_t data_size;
  handle_t root_handle;
  handle_t handle;
  void *base;
  uint64_t map_flags;
  uint32_t shmtype;
} nbl_shm_t;

static void test_license_load_inject_data(int slot, uint64_t flags,
                                          int src_offset, handle_t handle) {
  int rc;
  nbl_chn_t *chn = NULL;
  nbl_stm_t *cmd_request = NULL;
  nbl_stm_t *cmd_response = NULL;
  nbl_shm_t *shm = NULL;
  void *shm_base = NULL;
  uint32_t size = fuz_rnd() % 8192;

  rc = nbl_chn_open(LICENSE_NMT_TEE_PORT, NBL_TIMEOUT_INFINITY, &chn);
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  if (size > 0) {
    rc = nbl_shm_alloc(chn, size, NBL_SHM_PERM_RW, &shm, &shm_base);
    if (rc != NBL_NO_ERROR) {
      fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
      exit(-1);
    }
  }

  cmd_request = nbl_stm_create();
  if (ONE_IN(10)) {
    nbl_stm_add_int(cmd_request, fuz_rnd());
  } else {
    nbl_stm_add_int(cmd_request, NMT_TEE_CMD_LOAD_INJECT_DATA);
  }
  nbl_stm_add_int(cmd_request, slot);
  nbl_stm_add_buf(cmd_request, sizeof(flags), &flags);
  nbl_stm_add_int(cmd_request, size);
  nbl_stm_add_int(cmd_request, src_offset);
  nbl_stm_add_shm(cmd_request, shm);
  if (ONE_IN(10)) {
    shm->handle = handle;
  }

  cmd_response = nbl_stm_create();

  rc = nbl_chn_write_stm_sync(chn, cmd_request, cmd_response,
                              NBL_TIMEOUT_INFINITY);
  if (rc != NBL_NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    goto release;
  }

release:
  nbl_shm_free(shm);
  nbl_stm_destroy(cmd_request);
  nbl_stm_destroy(cmd_response);
  nbl_chn_close(chn);
}

struct function_description nbl_plt_load_inject_data_test = {
    .funcname = "nbl_plt_load_inject_data",
    .funcaddr = test_license_load_inject_data,
    .argnum = 4,
    .argname = {"slot", "flags", "src_offset", "handle"},
    .argtype = {ARG_INT, ARG_INT64, ARG_INT, ARG_TRUSTY_HANDLE}};
PrepareTest(nbl_plt_load_inject_data) { return &nbl_plt_load_inject_data_test; }
