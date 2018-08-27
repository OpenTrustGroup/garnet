// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "garnet/bin/gzos/ree_agent/message_reader.h"

namespace ree_agent {

static constexpr uint32_t kMaxServerNameLength = 256;
static constexpr uint32_t kMaxEndpointNumber = 1024;
static constexpr uint32_t kCtrlEndpointAddress = 4096;
static constexpr uint32_t kInvalidEndpointAddress = UINT32_MAX;

struct gz_ipc_msg_hdr {
  uint32_t src;
  uint32_t dst;
  uint32_t reserved;
  uint16_t len;
  uint16_t flags;
  uint8_t data[0];
} __PACKED;

enum class CtrlMessageType : uint32_t {
  CONNECT_REQUEST,
  CONNECT_RESPONSE,
  DISCONNECT_REQUEST,
};

struct gz_ipc_ctrl_msg_hdr {
  CtrlMessageType type;
  uint32_t body_len;
} __PACKED;

struct gz_ipc_conn_req_body {
  char name[kMaxServerNameLength];
} __PACKED;

struct gz_ipc_conn_rsp_body {
  uint32_t target;
  uint32_t status;
  uint32_t remote;
} __PACKED;

struct gz_ipc_disc_req_body {
  uint32_t target;
} __PACKED;

struct gz_ipc_channel_info {
  uint32_t remote;
} __PACKED;

struct gz_ipc_vmo_info {
  uint64_t paddr;
  uint64_t size;
} __PACKED;

enum class HandleType : uint32_t {
  CHANNEL,
  VMO,
};

struct gz_ipc_handle {
  HandleType type;
  union {
    gz_ipc_channel_info channel;
    gz_ipc_vmo_info vmo;
  };
} __PACKED;

struct gz_ipc_endpoint_msg_hdr {
  size_t num_handles;
  gz_ipc_handle handles[kDefaultHandleCapacity];
} __PACKED;

struct conn_req_msg {
  struct gz_ipc_ctrl_msg_hdr hdr;
  struct gz_ipc_conn_req_body body;
};

struct conn_rsp_msg {
  struct gz_ipc_ctrl_msg_hdr hdr;
  struct gz_ipc_conn_rsp_body body;
};

struct disc_req_msg {
  struct gz_ipc_ctrl_msg_hdr hdr;
  struct gz_ipc_disc_req_body body;
};

};  // namespace ree_agent
