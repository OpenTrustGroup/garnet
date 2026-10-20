// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utrace.h"
#include "us_comcat_trace.h"
#include "lib/fsl/handles/object_info.h"

static comcat_trace_buf_t* g_comcat_trace_buf = nullptr;

// Category and group mappings
static uint32_t const tracer_group[] = {
    [CAT_IO_SERVER] = GRP_COMCAT_LINK,
};

static void get_trace_buf(uint32_t tag) {
  auto trace = machina::Utrace::GetInstanceNoLock();
  if (!trace)
    return;
  FXL_CHECK(trace != nullptr);
  g_comcat_trace_buf = reinterpret_cast<comcat_trace_buf_t*>(
      trace->GetTraceBuf(tracer_group[NT_COMM_CAT(tag)]));
}

void us_comcat_trace_begin(uint32_t tag, uint64_t a, uint64_t b, uint64_t c) {
  get_trace_buf(tag);
  if (!g_comcat_trace_buf)
    return;
  if (!g_comcat_trace_buf->rb_hdr.enable)
    return;

  uint64_t offs = (uint64_t)(g_comcat_trace_buf->rb_hdr.write_pos.fetch_add(1));
  uint64_t wr = offs % g_comcat_trace_buf->rb_hdr.rec_cnt;

  g_comcat_trace_buf->rec[wr] = {
      .tag = tag,
      .a = a,
      .b = b,
      .c = c,
      .tid = (uint64_t)(fsl::GetCurrentThreadKoid()),
      .flag = 0,
      .timestamp = zx_ticks_get(),
  };
}

void us_comcat_trace_end(uint32_t tag, uint64_t a, uint64_t b, uint64_t c) {
  get_trace_buf(tag);
  if (!g_comcat_trace_buf)
    return;
  if (!g_comcat_trace_buf->rb_hdr.enable)
    return;

  uint64_t offs = (uint64_t)(g_comcat_trace_buf->rb_hdr.write_pos.fetch_add(1));
  uint64_t wr = offs % g_comcat_trace_buf->rb_hdr.rec_cnt;

  g_comcat_trace_buf->rec[wr] = {
      .tag = tag,
      .a = a,
      .b = b,
      .c = c,
      .tid = (uint64_t)(fsl::GetCurrentThreadKoid()),
      .flag = 1,
      .timestamp = zx_ticks_get(),
  };
}
