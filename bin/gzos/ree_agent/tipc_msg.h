// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

namespace ree_agent {

static constexpr int kTipcMaxServerNameLength = 256;

static constexpr uint32_t kTipcCtrlAddress = 53;
static constexpr uint32_t kTipcAddrMaxNum = 256;
static constexpr uint32_t kTipcAddrBase = 1000;

struct tipc_hdr {
  uint32_t src;
  uint32_t dst;
  uint32_t reserved;
  uint16_t len;
  uint16_t flags;
  uint8_t data[0];
} __PACKED;

/*
 * TIPC control message consists of common tipc_ctrl_msg_hdr
 * immediately followed by message specific body which also
 * could be empty.
 *
 * struct tipc_ctrl_msg {
 *    struct tipc_ctrl_msg_hdr hdr;
 *    uint8_t  body[0];
 * } __PACKED;
 *
 */
enum CtrlMessage {
  GO_ONLINE = 1,
  GO_OFFLINE = 2,
  CONNECT_REQUEST = 3,
  CONNECT_RESPONSE = 4,
  DISCONNECT_REQUEST = 5,
};

struct tipc_ctrl_msg_hdr {
  uint32_t type;
  uint32_t body_len;
} __PACKED;

struct tipc_conn_req_body {
  char name[kTipcMaxServerNameLength];
} __PACKED;

struct tipc_conn_rsp_body {
  uint32_t target;
  uint32_t status;
  uint32_t remote;
  uint32_t max_msg_size;
  uint32_t max_msg_cnt;
} __PACKED;

struct tipc_disc_req_body {
  uint32_t target;
} __PACKED;

}  // namespace ree_agent
