// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <assert.h>
#include <getopt.h>

#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nbl_fwk/err.h>
#include <nbl_fwk/log.h>
#include "nbl_fwk/ntime.h"

#define LOG_TAG "nbl_time.example"

// If want check memory leak, make MEM_CHECK valid.
//#define MEM_CHECK

#ifdef MEM_CHECK
extern void nbl_enable_trace_heap(void);
extern void nbl_disable_trace_heap(void);
extern void nbl_start_trace_heap(void);
extern int nbl_end_trace_heap(int try_fix);
#endif

void print_usage(char* self) {
  printf("Usage: %s, get or set time of TEE/REE/TA persistent time.\n", self);
  printf("  -h/--help show help information.\n");
  printf("  -s/--set <seconds.milliseconds> set ta persistent time.\n");
}

static const struct option opts[] = {{"set", required_argument, NULL, 's'},
                                     {"help", no_argument, NULL, 'h'},
                                     {0, 0, 0, 0}};

int main(int argc, char* argv[]) {
  int rlt;
  int opt;
  char* program = basename(argv[0]);
  bool set_ta_persist_time = false;

  nbl_time_t ta_time = {0, 0};
  nbl_time_t time;

#ifdef MEM_CHECK
  nbl_enable_trace_heap();
  nbl_start_trace_heap();
#endif

  // parse the args
  while ((opt = getopt_long(argc, argv, "s:h", opts, NULL)) != -1) {
    switch (opt) {
      case 's':
        set_ta_persist_time = true;
        char* p = strchr(optarg, '.');
        if (p) {
          ta_time.seconds = strtol(optarg, NULL, 10);
          ta_time.millis = atoi(p);
        } else {
          ta_time.seconds = atoi(optarg);
          ta_time.millis = 0;
        }
        break;
      case 'h':
        print_usage(program);
        return 0;
        break;
      default:
        print_usage(program);
        return 1;
    }
  }

  // get ree time.
  rlt = nbl_time_get_ree(&time);
  if (rlt == NBL_NO_ERROR) {
    NBL_LOGI("get REE time: %u.%u\n", time.seconds, time.millis);
  } else {
    NBL_LOGE("Cannot get REE time, error=%d\n", rlt);
  }

  // get tee system time.
  rlt = nbl_time_get_system(&time);
  if (rlt == NBL_NO_ERROR) {
    NBL_LOGI("get TEE system time: %u.%u\n", time.seconds, time.millis);
  } else {
    NBL_LOGE("Cannot get TEE system time, error=%d\n", rlt);
  }

  // set ta time if need.
  if (set_ta_persist_time) {
    rlt = nbl_time_set_ta_persistent(&ta_time);
    if (rlt == NBL_NO_ERROR) {
      NBL_LOGI("set this TA persistent time: %u.%u\n", ta_time.seconds,
               ta_time.millis);

    } else {
      NBL_LOGE("Cannot set this TA persistent time: %u.%u, error=%d\n",
               ta_time.seconds, ta_time.millis, rlt);
    }
  }

  // get ta time.
  rlt = nbl_time_get_ta_persistent(&time);
  if (rlt == NBL_NO_ERROR) {
    NBL_LOGI("get this TA persistent time: %u.%u\n", time.seconds, time.millis);
  } else if (rlt == NBL_ERR_NOT_FOUND) {
    NBL_LOGE("Don't set this TA persistent, so don't get.\n");
  } else {
    NBL_LOGE("Cannot get this TA persistent time, error=%d\n", rlt);
  }

#ifdef MEM_CHECK
  nbl_end_trace_heap(0);
  nbl_disable_trace_heap();
#endif

  return 0;
}
