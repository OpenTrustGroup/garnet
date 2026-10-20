// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_UTRACE_COMCAT_TRACE_H_
#define GARNET_LIB_MACHINA_UTRACE_COMCAT_TRACE_H_

#define COMCAT_TRACE_MAX_RECS 2048

#define COM_CAT_BIT_MASK(x) \
  (((x) >= sizeof(unsigned long) * 8) ? (0UL - 1) : ((1UL << (x)) - 1))
#define COM_CAT_BIT(x) (1 << (x))

#define TRACE_COMM_ID_EVENT_SHFT CAT_TOTAL_CNT
#define NT_COMM_CAT_MASK COM_CAT_BIT_MASK(TRACE_COMM_ID_EVENT_SHFT)
#define NT_COMM_CAT(tag) ((tag)&NT_COMM_CAT_MASK)
#define NT_COMM_EVENT(tag) \
  (((tag) & ~NT_COMM_CAT_MASK) >> TRACE_COMM_ID_EVENT_SHFT)

#define NT_COMM_TAG(category, event) \
  (((event) << TRACE_COMM_ID_EVENT_SHFT) | (category))

// category definition
enum trace_categorys {
  CAT_IO_SERVER = 0,
  CAT_TOTAL_CNT,
};

#define TAG_IO_SERVER COM_CAT_BIT(CAT_IO_SERVER)

// comcat trace events
enum comcat_nt_events {
  EVT_SYNC_IO_REQ = 0,
  EVT_CHANNEL_BUSY,
  EVT_INJECT_VIRQ,
  EVT_ASYNC_IO_REQ,
  EVT_ASYNC_RESP,
  EVT_ASYNC_IO_COMPLETE,
};

// trace id
#define TID_IO_SERVER_SYNC_IO_REQ NT_COMM_TAG(TAG_IO_SERVER, EVT_SYNC_IO_REQ)
#define TID_IO_SERVER_CHAN_BUSY NT_COMM_TAG(TAG_IO_SERVER, EVT_CHANNEL_BUSY)
#define TID_IO_SERVER_INJECT_VIRQ NT_COMM_TAG(TAG_IO_SERVER, EVT_INJECT_VIRQ)
#define TID_IO_SERVER_ASYNC_IO_REQ NT_COMM_TAG(TAG_IO_SERVER, EVT_ASYNC_IO_REQ)
#define TID_IO_SERVER_ASYNC_RESP NT_COMM_TAG(TAG_IO_SERVER, EVT_ASYNC_RESP)
#define TID_IO_SERVER_IO_COMPLETE \
  NT_COMM_TAG(TAG_IO_SERVER, EVT_ASYNC_IO_COMPLETE)

// ring buffer comcat trace layout:
// ---------------------------------------------------------------
// |  comcat trace_ringbuf_hdr  | rec[0] | rec[1] | ...... | rec[n] |
// ---------------------------------------------------------------
//                           | <-             size            -> |
// | <-      hdr_size     -> |
//                           | <-           write_pos         -> |
typedef struct {
  std::atomic<uint64_t> write_pos;
  uint64_t read_pos;
  size_t hdr_size;
  size_t size;
  size_t rec_size;
  uint32_t rec_cnt;
  bool enable;
  uint8_t reserved[3];
} comcat_trace_ringbuf_hdr_t;

typedef struct {
  uint32_t tag;
  uint64_t a;
  uint64_t b;
  uint64_t c;
  uint64_t tid;
  // 50 bits timestamp is big enough, for 13MHz tick, it can record
  // for more than 5 years.
  uint64_t flag : 1;
  uint64_t timestamp : 50;
} comcat_rec_t;

typedef struct {
  comcat_trace_ringbuf_hdr_t rb_hdr;
  comcat_rec_t rec[COMCAT_TRACE_MAX_RECS];
} comcat_trace_buf_t;

// void init_comcat_trace(void);
void us_comcat_trace_begin(uint32_t tag, uint64_t a, uint64_t b, uint64_t c);

void us_comcat_trace_end(uint32_t tag, uint64_t a, uint64_t b, uint64_t c);
#endif