// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// refer to: //zircon/system/uapp/psutils/top.c

#include <pretty/sizes.h>
#include <task-utils/walker.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cstdlib>
#include "top.h"

enum sort_order { UNSORTED, SORT_TIME_DELTA };

static top_buf_t __global_top_info;
static top_buf_t* get_nbl_top_trace(void) {
  return &__global_top_info;
}

typedef struct {
  struct list_node node;

  // has it been seen this pass?
  bool scanned;
  zx_time_t delta_time;

  // information about the thread
  zx_koid_t proc_koid;
  zx_koid_t koid;
  zx_info_thread_t info;
  zx_info_thread_stats_t stats;
  char name[ZX_MAX_NAME_LEN];
  char proc_name[ZX_MAX_NAME_LEN];
} thread_info_t;

// arguments
static std::atomic<zx_time_t> delay;
static zx_time_t thread_delay = ZX_MSEC(500);
static int count = -1;
static bool print_all = false;
static enum sort_order sort_order = SORT_TIME_DELTA;

// active locals
static struct list_node thread_list = LIST_INITIAL_VALUE(thread_list);
static char last_process_name[ZX_MAX_NAME_LEN];
static zx_koid_t last_process_scanned;

// Return text representation of thread state.
static const char* state_string(const zx_info_thread_t* info) {
  if (info->wait_exception_port_type != ZX_EXCEPTION_PORT_TYPE_NONE) {
    return "excp";
  } else {
    switch (info->state) {
      case ZX_THREAD_STATE_NEW:
        return "new";
      case ZX_THREAD_STATE_RUNNING:
        return "run";
      case ZX_THREAD_STATE_SUSPENDED:
        return "susp";
      case ZX_THREAD_STATE_BLOCKED:
        return "block";
      case ZX_THREAD_STATE_DYING:
        return "dying";
      case ZX_THREAD_STATE_DEAD:
        return "dead";
      default:
        return "???";
    }
  }
}

static zx_status_t process_callback(void* unused_ctx,
                                    int depth,
                                    zx_handle_t proc,
                                    zx_koid_t koid,
                                    zx_koid_t parent_koid) {
  last_process_scanned = koid;

  zx_status_t status = zx_object_get_property(
      proc, ZX_PROP_NAME, &last_process_name, sizeof(last_process_name));
  return status;
}

// Adds a thread's information to the thread_list
static zx_status_t thread_callback(void* unused_ctx,
                                   int depth,
                                   zx_handle_t thread,
                                   zx_koid_t koid,
                                   zx_koid_t parent_koid) {
  thread_info_t e = {};

  e.koid = koid;
  e.scanned = true;

  e.proc_koid = last_process_scanned;
  strlcpy(e.proc_name, last_process_name, sizeof(e.proc_name));

  zx_status_t status =
      zx_object_get_property(thread, ZX_PROP_NAME, e.name, sizeof(e.name));
  if (status != ZX_OK) {
    return status;
  }
  status = zx_object_get_info(thread, ZX_INFO_THREAD, &e.info, sizeof(e.info),
                              NULL, NULL);
  if (status != ZX_OK) {
    return status;
  }
  status = zx_object_get_info(thread, ZX_INFO_THREAD_STATS, &e.stats,
                              sizeof(e.stats), NULL, NULL);
  if (status != ZX_OK) {
    return status;
  }

  // see if this thread is in the list
  thread_info_t* temp;
  list_for_every_entry(&thread_list, temp, thread_info_t, node) {
    if (e.koid == temp->koid) {
      // mark it scanned, compute the delta time,
      // and copy the new state over
      temp->scanned = true;
      temp->delta_time = e.stats.total_runtime - temp->stats.total_runtime;
      temp->info = e.info;
      temp->stats = e.stats;
      return ZX_OK;
    }
  }

  // it wasn't in the list, add it
  thread_info_t* new_entry =
      static_cast<thread_info_t*>(malloc(sizeof(thread_info_t)));
  *new_entry = e;

  list_add_tail(&thread_list, &new_entry->node);

  return ZX_OK;
}

static void sort_threads(enum sort_order order) {
  if (order == UNSORTED)
    return;

  struct list_node new_list = LIST_INITIAL_VALUE(new_list);

  // cheezy sort into second list, then swap back to first
  thread_info_t* e;
  while ((e = list_remove_head_type(&thread_list, thread_info_t, node))) {
    thread_info_t* t;

    bool found = false;
    list_for_every_entry(&new_list, t, thread_info_t, node) {
      if (order == SORT_TIME_DELTA) {
        if (e->delta_time > t->delta_time) {
          list_add_before(&t->node, &e->node);
          found = true;
          break;
        }
      }
    }

    // walked off the end
    if (!found)
      list_add_tail(&new_list, &e->node);
  }

  list_move(&new_list, &thread_list);
}

static void print_threads(void) {
  thread_info_t* e;
  top_buf_t* tt = get_nbl_top_trace();

  int i = 0;
  tt->write_done = false;

  list_for_every_entry(&thread_list, e, thread_info_t, node) {
    // only print threads that are active
    if (!print_all && e->delta_time == 0)
      continue;

    uint32_t percent = 0;
    if (e->delta_time > 0)
      percent = e->delta_time / (double)delay * 100 * 100;
    tt->status[i].PID = e->proc_koid;
    tt->status[i].TID = e->koid;
    tt->status[i].percent = percent;
    strncpy(tt->status[i].state, state_string(&e->info), STATE_INFO_LEN);
    strncpy(tt->status[i].proc_name, e->proc_name, PROC_NAME_LEN);
    strncpy(tt->status[i].thread_name, e->name, THREAD_NAME_LEN);

    // only print the first count items (or all, if count < 0)
    if (++i == count)
      break;
  }
  tt->write_done = true;
}

int nbl_loading_show_task(void* ctx) {
  int ret = 0;
  int num_loops = -1;
  bool first_run = true;
  // 1000ms
  zx_time_t period = ZX_SEC(1);
  fbl::unique_ptr<top_task_args_t> args(static_cast<top_task_args_t*>(ctx));
  machina::Utrace* trace = args->trace;
  top_buf_t* tt = get_nbl_top_trace();

  if (!trace) {
    return ZX_ERR_NO_MEMORY;
  }

  delay.store(ZX_SEC(1));

  print_all = true;
  count = TOP_THREAD_COUNT;
  sort_order = SORT_TIME_DELTA;

  top_buf_t* pbuffer =
      reinterpret_cast<top_buf_t*>(trace->GetInstance()->GetTraceBuf(GRP_TOP));
  if (!pbuffer)
    return ZX_ERR_NO_MEMORY;

  for (;;) {
    if (pbuffer->enable == false) {
      zx_nanosleep(zx_deadline_after(thread_delay));

      // clear top info that invalid
      if (first_run == false) {
        first_run = true;
        memset(&(pbuffer->status), 0,
               sizeof(trace_thread_stat_t) * TOP_THREAD_COUNT);
      }

      continue;
    }

    period = ZX_MSEC((uint32_t)pbuffer->period.load());
    if (period < ZX_MSEC(100) || period > ZX_SEC(30)) {
      period = ZX_SEC(1);
      pbuffer->period.store(1000);
    }
    delay.store(period);
    zx_time_t next_deadline = zx_deadline_after(delay);

    // mark all active threads as not scanned
    thread_info_t* e;
    list_for_every_entry(&thread_list, e, thread_info_t, node) {
      e->scanned = false;
    }

    // iterate the entire job tree
    zx_status_t status =
        walk_root_job_tree(NULL, process_callback, thread_callback, NULL);
    if (status != ZX_OK) {
      fprintf(stderr, "WARNING: walk_root_job_tree failed: %s (%d)\n",
              zx_status_get_string(status), status);
      ret = 1;
    }

    // remove every entry that hasn't been scanned this pass
    thread_info_t* temp;
    list_for_every_entry_safe(&thread_list, e, temp, thread_info_t, node) {
      if (!e->scanned) {
        list_delete(&e->node);
        free(e);
      }
    }

    if (first_run) {
      // We don't have data until after we scan twice, since we're
      // computing deltas.
      first_run = false;
      continue;
    }

    // sort the list
    sort_threads(sort_order);

    // dump the list of threads
    print_threads();

    memcpy(&(pbuffer->status), &(tt->status),
           sizeof(trace_thread_stat_t) * TOP_THREAD_COUNT);

    if (num_loops > 0) {
      if (--num_loops == 0) {
        break;
      }
    }

    zx_nanosleep(next_deadline);
  }

  return ret;
}
