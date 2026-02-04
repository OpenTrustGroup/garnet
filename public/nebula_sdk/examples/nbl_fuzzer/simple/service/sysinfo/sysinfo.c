// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * sysinfo API is NOT public.
 * Implemented in service.c of nbl_fwk.
 */

#include <fuz_common.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <nbl_fwk/err.h>
#include <nbl_fwk/log.h>

#include <service_utils.h>

#define LOG_TAG "nbl_sys_info"

/**
 * Because nbl_sys_get_info just get information by trusty_ioctl from server
 * actually, so NO need to test it, now just reimplement it here, and get
 * information. trusty_ioctl is tested in other file.
 */

#define NBL_SYS_INFO_VERSION_V1 1
#define NBL_SYS_INFO_CUR_VERSION NBL_SYS_INFO_VERSION_V1

typedef struct nbl_sys_info_v1 {
  int32_t version;
  uint32_t shmtype;
  uint32_t vid;
  uint32_t pid;
  uint32_t rpmb_programmed;
} nbl_sys_info_v1_t;

typedef nbl_sys_info_v1_t nbl_sys_info_t;

typedef struct {
  uint32_t save_type;
  uint32_t shm_type;
} dcfg_hyp_nss_t;

typedef struct {
  uint32_t base; /* in bytes */
  uint32_t size; /* in bytes */
  uint32_t programmed;
} dcfg_hyp_rpmb_t;

int nbl_sys_get_info_fuzzer() {
  int rc;
  uint32_t vid;
  uint32_t pid;
  dcfg_hyp_nss_t nss_info;
  dcfg_hyp_rpmb_t rpmb_info;

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_VENDOR_ID, &vid);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Get vid failed (%d).\n", vid);
    rc = NBL_ERR_FAULT;
    goto failed;
  }

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_PRODUCT_ID, &pid);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Get pid failed (%d).\n", pid);
    rc = NBL_ERR_FAULT;
    goto failed;
  }

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_NSS_INFO, &nss_info);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Get nss info failed (%d).\n", rc);
    rc = NBL_ERR_FAULT;
    goto failed;
  }

  rc = _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_GET_RPMB_INFO, &rpmb_info);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Get rpmb info failed (%d).\n", rc);
    rc = NBL_ERR_FAULT;
    goto failed;
  }

failed:
  return rc;
}

////////////////////////////////////////////////////////////
// Tester

struct function_description nbl_sys_get_info_test = {
    .funcname = "nbl_sys_get_info",
    .funcaddr = nbl_sys_get_info_fuzzer,
    .argnum = 0,
    .argname = {},
};

PrepareTest(nbl_sys_get_info) { return &nbl_sys_get_info_test; }
