// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <nbl_drv/core.h>
#include <nbl_drv/hal.h>

static fb_info_t g_fbi;
static iobuf_t g_iobuf;

static void FbFlushCallback() {
  // TODO: flush framebuffer to display hw
}

static void InitFb() {
  nbl_status_t rc = nbl_drv_get_fb_info(&g_fbi);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_get_fb_info failed %d!\n", rc);
    abort();
  }

  rc = nbl_drv_set_fb_flush_cb(&FbFlushCallback);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_set_fb_flush_cb failed %d!\n", rc);
    abort();
  }
}

static void InitMmio() {
  if (nbl_drv_get_iobuf_count() == 0) {
    nbl_drv_loge("nbl_drv_get_iobuf_count == 0, abort!\n");
    abort();
  }

  nbl_status_t rc = nbl_drv_get_iobuf_info(0, &g_iobuf);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_get_iobuf_info failed %d, abort()!\n", rc);
    abort();
  }

  rc = nbl_drv_map_iobuf(&g_iobuf, CACHE_POLICY_UNCACHED_DEVICE);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_map_iobuf failed %d, abort()!\n", rc);
    abort();
  }
  printf("iobuf.phys: %#lx iobuf.virt: %p iobuf.offset: %#lx iobuf.size: %#zx "
         "mapped.\n",
         g_iobuf.phys, g_iobuf.virt, g_iobuf.offset, g_iobuf.size);
}

static void CleanupMmio() {
  nbl_status_t rc = nbl_drv_unmap_iobuf(&g_iobuf);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_unmap_iobuf failed %d!\n", rc);
  }
}

void DrvInit() {
  InitFb();
  InitMmio();
}

void DrvResume() {}

void DrvPause() {}

void DrvExit() {
  // NOTE: tui-drvfwk will cleanup fb resource
  CleanupMmio();
}
