// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * This case has NOT effect.
 *
 * Try to use Flush-Reload method on side channel attack.
 * 1. Make shared memory with "app".
 * 2. Flush and invalid shared memory.
 * 	Code in "zx_cache_flush" is got from zircon source.
 * 	Code in "zx_cache_flush1" is used to fill all cache by accessing large memory in REE.
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
#include <time.h>
#include <unistd.h>

#include <nbl_fwk_ree/service.h>

#include "../common.h"

#define LOG_TAG "cache ca"

#define SHM_MSG "hello service!"

#define ZX_CLOCK_MONOTONIC 1
typedef unsigned long zx_time_t;
zx_time_t zx_clock_get(int clock) {
  struct timespec tp;
  int rlt = clock_gettime(CLOCK_MONOTONIC, &tp);
  if (rlt) {
    return 0;
  }

  return tp.tv_sec * 1000000000 + tp.tv_nsec;
}

namespace {

struct vdso_constants {
  // Number of bytes in a data cache line.
  uint32_t dcache_line_size;

  // Number of bytes in an instruction cache line.
  uint32_t icache_line_size;
};
const struct vdso_constants DATA_CONSTANTS = {64, 64};

template <typename T>
void for_each_cache_line(const void* addr,
                         size_t len,
                         uint32_t line_size,
                         T func) {
  for (uintptr_t p = (uintptr_t)addr & -(uintptr_t)line_size;
       p < (uintptr_t)addr + len; p += line_size) {
    func(p);
  }
}

template <typename T>
void for_each_dcache_line(const void* addr, size_t len, T func) {
  for_each_cache_line(addr, len, DATA_CONSTANTS.dcache_line_size, func);
}

template <typename T>
void for_each_icache_line(const void* addr, size_t len, T func) {
  for_each_cache_line(addr, len, DATA_CONSTANTS.icache_line_size, func);
}

}  // anonymous namespace

#define ZX_OK (0)
#define ZX_ERR_INVALID_ARGS (-10)

#define ZX_CACHE_FLUSH_INSN (1u << 0)
#define ZX_CACHE_FLUSH_DATA (1u << 1)
#define ZX_CACHE_FLUSH_INVALIDATE (1u << 2)
int zx_cache_flush(const void* addr, size_t len, uint32_t flags) {
  switch (flags) {
    case ZX_CACHE_FLUSH_INSN:
      break;
    case ZX_CACHE_FLUSH_DATA:
      break;
    case ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INSN:
      break;
    case ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE:
      break;
    case ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE | ZX_CACHE_FLUSH_INSN:
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  if (flags & ZX_CACHE_FLUSH_DATA) {
    // Flush data to the point of coherency which effectively means
    // making sure cache data is written back to main mmemory and optionally
    // invalidated.
    if (flags & ZX_CACHE_FLUSH_INVALIDATE) {
      for_each_dcache_line(addr, len, [](uintptr_t p) {
        // Clean and invalidate data cache to point of coherency.
        __asm__ volatile("dc civac, %0" ::"r"(p));
      });
    } else {
      for_each_dcache_line(addr, len, [](uintptr_t p) {
        // Clean data cache (dc) to point of coherency (cvac).
        __asm__ volatile("dc cvac, %0" ::"r"(p));
      });
    }
    // Ensure the cache flush has completed with regards to point of coherency
    __asm__ volatile("dsb ish");
  }

  if (flags & ZX_CACHE_FLUSH_INSN) {
    // If we didn't already clean the dcache all the way to the point
    // of coherency, clean it the point to unification.
    // Point of unification is the level within the cache heirarchy where the
    // the instruction and data cache are no longer separate. This is usually
    // L2.
    if (!(flags & ZX_CACHE_FLUSH_DATA)) {
      for_each_dcache_line(addr, len, [](uintptr_t p) {
        // Clean data cache (dc) to point of unification (cvau).
        __asm__ volatile("dc cvau, %0" ::"r"(p));
      });
      // Synchronize the dcache flush to before the icache flush.
      __asm__ volatile("dsb ish");
    }

    for_each_icache_line(addr, len, [](uintptr_t p) {
      // Invalidate instruction cache (ic) to point of unification (ivau).
      __asm__ volatile("ic ivau, %0" ::"r"(p));
    });
    // Synchronize the icache flush to before future instruction fetches.
    __asm__ volatile("isb sy");
  }

  return ZX_OK;
}

int zx_cache_flush1(const void* addr, size_t len, uint32_t flags) {
  const int SIZE = 64 * 1024 * 1024;
  static char* p = NULL;

  if (!p)
    p = (char*)malloc(SIZE);
  if (p == NULL) {
    NBL_LOGE("Failed to malloc %d.\n", SIZE);
    return ZX_ERR_INVALID_ARGS;
  }

  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < SIZE; j++) {
      p[j] += i * j;
    }
  }

  return ZX_OK;
}

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
      zx_time_t start, end;
      start = zx_clock_get(ZX_CLOCK_MONOTONIC);
      ch = data[i];
      end = zx_clock_get(ZX_CLOCK_MONOTONIC);
      times[j] = end - start;
      j++;

      // NBL_LOGI("START(%lu) END(%lu).\n", start, end);
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
}

// Connect to service and create shared memory.
static int _run_service_a_demo() {
  int rc = NBL_NO_ERROR;
  nbl_chn_t* chn;
  const char* srv_name = SRV_NAME;
  int cmd;
  nbl_stm_t* request;
  nbl_stm_t* response;
  nbl_shm_t* shm = NULL;
  void* shm_base;
  int ack;

  const int RUN_COUNT = 10;
  int hits[STEP_COUNT];
  int hit = 0;

  rc = nbl_chn_open(srv_name, 0, &chn);
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
    analysis((char*)shm_base, hits);
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
