// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <nbl_drv/core.h>
#include <nbl_drv/hal.h>

static irq_t g_irq;
static iobuf_t g_iobuf;
static pthread_t g_irq_thread;

static void *IrqHandler(void *arg) {
  nbl_drv_logi("irq thread starting...\n");
  irq_t *irq = arg;
  for (;;) {
    nbl_status_t rc = nbl_drv_wait_irq(irq);
    if (rc != NBL_NO_ERROR) {
      nbl_drv_loge("nbl_drv_wait_irq failed %d!\n", rc);
      break;
    }
    // TODO: for example read registers, report touch
    // input_event_t event;
    // nbl_drv_queue_input_event(&event);
  }
  nbl_drv_logi("irq thread exiting...\n");
  return NULL;
}

static void InitIrq() {
  if (nbl_drv_get_irq_count() == 0) {
    nbl_drv_loge("nbl_drv_get_irq_count == 0, abort!\n");
    abort();
  }

  nbl_status_t rc = nbl_drv_get_irq_info(0, &g_irq);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_get_irq_info failed %d, abort()!\n", rc);
    abort();
  }

  rc = nbl_drv_map_irq(&g_irq);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_map_irq failed %d, abort()!\n", rc);
    abort();
  }
  printf("irq.irq: %u irq.mode: %u mapped.\n", g_irq.irq, g_irq.mode);

  pthread_create(&g_irq_thread, NULL, IrqHandler, &g_irq);
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

static void CleanupIrq() {
  nbl_status_t rc = nbl_drv_unmap_irq(&g_irq);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_unmap_irq failed %d!\n", rc);
  }
  pthread_join(g_irq_thread, NULL);
}

static void CleanupMmio() {
  nbl_status_t rc = nbl_drv_unmap_iobuf(&g_iobuf);
  if (rc != NBL_NO_ERROR) {
    nbl_drv_loge("nbl_drv_unmap_iobuf failed %d!\n", rc);
  }
}

void DrvInit() {
  InitIrq();
  InitMmio();
}

void DrvResume() {}

void DrvPause() {}

void DrvExit() {
  CleanupIrq();
  CleanupMmio();
}
