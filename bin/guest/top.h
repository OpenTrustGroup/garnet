// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_TOP_H_
#define GARNET_BIN_GUEST_TOP_H_
#include "garnet/lib/machina/utrace.h"

typedef struct top_task_args {
  machina::Utrace* trace;
} top_task_args_t;

#define STATE_INFO_LEN 10
#define PROC_NAME_LEN 15
#define THREAD_NAME_LEN 35
typedef struct {
  uint32_t PID;
  uint32_t TID;
  uint32_t percent;
  char state[STATE_INFO_LEN];         // state string, 10 byte is enough
  char proc_name[PROC_NAME_LEN];      // proc name, size 15 is enough
  char thread_name[THREAD_NAME_LEN];  // thread name, size 35 is enough
} trace_thread_stat_t;

#define TOP_THREAD_COUNT 50
typedef struct {
  trace_thread_stat_t status[TOP_THREAD_COUNT];  // currently 50 is enough
  bool write_done;
  bool read_done;
  bool enable;
  std::atomic<uint32_t> period;
} top_buf_t;

int nbl_loading_show_task(void* ctx);

#endif  // GARNET_BIN_GUEST_TOP_H_
