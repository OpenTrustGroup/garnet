// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gz_ipc_server.h"

namespace ree_agent {

zx_status_t GzIpcServer::HandleConnectRequest(uint32_t remote_addr,
                                              gz_ipc_ctrl_msg_hdr* ctrl_hdr) {
  FXL_CHECK(ctrl_hdr);

  if (ctrl_hdr->body_len != sizeof(gz_ipc_conn_req_body)) {
    FXL_LOG(ERROR) << "Invalid disc req msg";
    return ZX_ERR_INTERNAL;
  }

  zx::channel ch0, ch1;
  zx_status_t status = zx::channel::create(0, &ch0, &ch1);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create channel pair, status=" << status;
    return status;
  }

  status = AllocEndpoint(std::move(ch0), remote_addr);
  if (status != ZX_OK) {
    return status;
  }

  auto conn_req = reinterpret_cast<gz_ipc_conn_req_body*>(ctrl_hdr + 1);
  ta_service_provider_.ConnectToService(std::move(ch1), conn_req->name);

  return ZX_OK;
}

zx_status_t GzIpcServer::HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) {
  uint32_t remote = msg_hdr->src;
  uint32_t msg_size = msg_hdr->len;

  if (msg_size < sizeof(gz_ipc_ctrl_msg_hdr)) {
    FXL_LOG(ERROR) << "Invalid ctrl msg";
    return ZX_ERR_INTERNAL;
  }

  auto ctrl_hdr = reinterpret_cast<gz_ipc_ctrl_msg_hdr*>(msg_hdr->data);

  switch (ctrl_hdr->type) {
    case CtrlMessageType::CONNECT_REQUEST:
      return HandleConnectRequest(remote, ctrl_hdr);

    case CtrlMessageType::DISCONNECT_REQUEST:
      return HandleDisconnectRequest(ctrl_hdr);

    default:
      FXL_LOG(ERROR) << "Invalid ctrl msg type";
      return ZX_ERR_INVALID_ARGS;
  }
}

}  // namespace ree_agent
